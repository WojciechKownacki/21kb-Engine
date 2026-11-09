"""Verify actionable diagnostics, model goldens, reimport and cooked semantics."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('engine-tests', 'renderer-tests', 'editor', 'cli', 'cooker', 'player', 'shaderc', 'engine-root', 'test-root'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--plugin', type=Path, action='append', default=[])
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='ImportDiagnostics', dir=args.test_root)).resolve()
    commands = []

    def run(exe, *tail, fails=False):
        command = list(map(str, [exe.resolve(), *tail]))
        result = subprocess.run(command, cwd=exe.resolve().parent, capture_output=True,
                                encoding='utf-8', errors='replace', timeout=240)
        commands.append(dict(command=command, exitCode=result.returncode, expectedFailure=fails,
                             stdout=result.stdout, stderr=result.stderr))
        (root / 'commands.json').write_text(json.dumps(commands, indent=2), encoding='utf-8')
        assert (result.returncode != 0) if fails else (result.returncode == 0), commands[-1]
        return result.stdout + result.stderr

    run(args.engine_tests, 'skeletal-mesh-assets')
    for suite in ('gltf-external', 'mesh-bake', 'import-catalog'):
        run(args.renderer_tests, suite)
    tests = Path(__file__).resolve().parent
    run(args.editor, '--selftest-scenario', tests / 'ImportDiagnosticsScenario.json',
        '--selftest-root', root, '--selftest-task', 'GUI')
    project = root / 'GUI/workspace'
    run(args.editor, '--selftest-scenario', tests / 'AssetReimportScenario.json',
        '--selftest-root', root, '--selftest-task', 'Reimport')
    refused = run(args.cli, 'component', 'add', '--project', project,
        '--entity', 'DiagnosticProbe', '--type', 'Light', fails=True)
    assert 'DiagnosticProbe' in refused and 'Fix:' in refused
    run(args.cli, 'component', 'set', '--project', project, '--entity', 'DiagnosticProbe',
        '--type', 'Light', '--set', 'intensity=3')
    source = root / 'RepairCli.gltf'
    source.write_text('{', encoding='utf-8')
    refused = run(args.cli, 'import', '--project', project, '--destination', '/Game/Models', source, fails=True)
    assert source.name in refused and 'Fix:' in refused and 'Retry: kb_cli import' in refused
    valid = json.loads((tests / 'AssetReimportScenario.json').read_text())['steps'][1]['content']
    source.write_text(valid, encoding='utf-8')
    run(args.cli, 'import', '--project', project, '--destination', '/Game/Models', source)
    script = project / 'Assets/Logic/CompileProbe.lua'
    good = script.read_text(encoding='utf-8')
    script.write_text('function Tick(self, dt)\n local broken =\nend\n', encoding='utf-8')
    try:
        refused = run(args.cli, 'run', '--project', project, '--frames', '4', fails=True)
        assert 'CompileProbe.lua' in refused and 'Fix: Correct the Lua source' in refused
    finally:
        script.write_text(good, encoding='utf-8')
    run(args.cli, 'run', '--project', project, '--frames', '4')
    golden = root / 'GoldenSource'
    run(args.engine_tests, 'write-import-golden', golden)
    run(args.cli, 'import', '--project', project, '--destination', '/Game/Golden',
        '--skeletal', 'yes', '--import-materials', 'yes', golden / 'HeroExtended.gltf')
    axis = golden / 'Axis.obj'
    axis.write_text('v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n', encoding='utf-8')
    run(args.cli, 'import', '--project', project, '--destination', '/Game/Golden',
        '--scale', '2', '--rotation', '0,90,0', axis)
    run(args.cli, 'component', 'add', '--project', project, '--entity', 'DiagnosticProbe',
        '--type', 'MeshRenderer')
    run(args.cli, 'component', 'set', '--project', project, '--entity', 'DiagnosticProbe',
        '--type', 'MeshRenderer', '--set', 'mesh=/Game/Golden/HeroExtended.kbskeletalmesh')
    run(args.cli, 'animator', 'new', '--project', project, '--name', 'ImportGolden',
        '--clip', '/Game/Golden/HeroExtended_Animation_0.kbanim')
    run(args.cli, 'component', 'add', '--project', project, '--entity', 'DiagnosticProbe', '--type', 'Animator')
    run(args.cli, 'component', 'set', '--project', project, '--entity', 'DiagnosticProbe',
        '--type', 'Animator', '--set', 'controller=/Game/Animation/ImportGolden.kbanimcontroller')
    material = next((project / 'Assets/Models/Materials').glob('Repair_Skin_*.kbmat'))
    run(args.cli, 'component', 'set', '--project', project, '--entity', 'Plane', '--type', 'MeshRenderer',
        '--set', 'mesh=/Game/Golden/Axis.21kb',
        '--set', 'material=/Game/' + material.relative_to(project / 'Assets').as_posix())
    package = root / 'Package'
    package.mkdir()
    run(args.cooker, '--project', project, '--target', 'Windows.x64', '--output', package / 'Game.kbpack',
        '--engine-root', args.engine_root.resolve(), '--shaderc', args.shaderc.resolve(), '--cache', root / 'CookCache')
    shutil.copy2(args.player, package / args.player.name)
    for dll in [*args.player.resolve().parent.glob('*.dll'), *args.plugin]:
        shutil.copy2(dll, package / dll.name)
    run(Path(sys.executable), args.engine_root / 'scripts/third_party_notices.py',
        '--engine-root', args.engine_root.resolve(), '--product', 'engine', '--platform', 'windows',
        '--product-name', 'Import Sample', '--version', '1.0', '--out', package)
    notices = (package / 'THIRD_PARTY_NOTICES.txt').read_text(encoding='utf-8')
    assert 'ufbx' in notices and 'cgltf' in notices
    hidden = root / 'SourceUnavailable'
    assert project.resolve().is_relative_to(root) and hidden.resolve().is_relative_to(root)
    project.rename(hidden)
    try:
        package_result = run(args.engine_tests, 'import-package-golden', package / 'Game.kbpack')
        static_result = run(args.renderer_tests, 'import-package-golden', package / 'Game.kbpack')
        assert 'static_import_package_golden topology=PASS' in static_result
        assert 'import_package_golden axes=PASS' in package_result and 'morph=PASS' in package_result
        output = run(package / args.player.name, '--headless', '--frames=12', '--width=64', '--height=64')
        assert 'errors=0' in output
    finally:
        hidden.rename(project)
    for relative in ('third_party/ufbx/LICENSE', 'third_party/bgfx.cmake/bgfx/3rdparty/cgltf/LICENSE'):
        license_text = (args.engine_root / relative).read_text(encoding='utf-8')
        assert 'Permission is hereby granted' in license_text
    result = dict(result='PASS', cliRecovery=True, guiRecovery=True, goldenPackage=package_result.strip(),
                  reimportGui=True, staticGolden=static_result.strip(), sourceUnavailableDuringPackageRun=True,
                  importerLicenses={'ufbx': 'MIT', 'cgltf': 'MIT', 'OBJ': 'engine MIT'})
    (root / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(root)


if __name__ == '__main__':
    main()
