"""Run prefab editing, relocation and independent copy in GUI, CLI and package."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('editor', 'cli', 'cooker', 'player', 'engine-root', 'test-root'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--plugin', type=Path, action='append', default=[])
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='PrefabLifecycle', dir=args.test_root)).resolve()
    history = []

    def run(exe, *tail):
        command = list(map(str, [exe.resolve(), *tail]))
        result = subprocess.run(command, cwd=exe.resolve().parent, capture_output=True,
                                encoding='utf-8', errors='replace', timeout=180)
        history.append(dict(command=command, exitCode=result.returncode,
                            stdout=result.stdout, stderr=result.stderr))
        (root / 'commands.json').write_text(json.dumps(history, indent=2), encoding='utf-8')
        assert result.returncode == 0, history[-1]
        return result.stdout + result.stderr

    run(args.editor, '--selftest-scenario', Path(__file__).with_name('PrefabWorkflowScenario.json').resolve(),
        '--selftest-root', root, '--selftest-task', 'GUI')
    project = root / 'GUI/workspace'
    assert not (project / 'Assets/Prefabs/Shared.kbprefab').exists()
    assert not (project / 'Assets/Prefabs/Prototype.kbprefab').exists()
    relocated = project / 'Assets/Relocated/Renamed.kbprefab'
    assert relocated.is_file() and (project / 'Assets/Relocated/Prototype.kbprefab').is_file()
    source = '''
function Ready(self, dt)
    for _, name in ipairs({"Nested", "Copy", "FirstHolder", "SecondHolder"}) do
        assert(World.FindByName(name) ~= 0, "Missing prefab object " .. name)
    end
    Log("prefab-lifecycle-pass")
end
'''
    (project / 'Assets/Scripts').mkdir(exist_ok=True)
    (project / 'Assets/Scripts/Probe.lua').write_text(source, encoding='utf-8')
    run(args.cli, 'scene-attach', '--project', project, '--scene', '/Game/Scenes/PrefabWorkflow.21kbscene',
        '--node', 'Camera', '--script', '/Game/Scripts/Probe.lua')
    assert 'prefab-lifecycle-pass' in run(args.cli, 'run', '--project', project, '--frames', '2')
    package = root / 'Package'
    package.mkdir()
    run(args.cooker, '--project', project, '--target', 'Windows.x64', '--output', package / 'Game.kbpack',
        '--engine-root', args.engine_root.resolve(), '--cache', root / 'CookCache',
        '--runtime-modules-output', package / 'RuntimeModules')
    shutil.copy2(args.player, package / args.player.name)
    for dll in [*args.player.resolve().parent.glob('*.dll'), *args.plugin]:
        shutil.copy2(dll, package / dll.name)
    hidden = root / 'SourceUnavailable'
    assert project.resolve().is_relative_to(root) and hidden.resolve().is_relative_to(root)
    project.rename(hidden)
    try:
        output = run(package / args.player.name, '--headless', '--frames=8')
        assert 'prefab-lifecycle-pass' in output and 'errors=0' in output and 'rendered=8' in output, output
    finally:
        hidden.rename(project)
    (root / 'result.json').write_text(json.dumps(dict(result='PASS', gui=True, cli=True,
        packaged=True, originalPrefabRemoved=True, movedPrefabSourceRemoved=True,
        projectSourcesUnavailableDuringPackageRun=True), indent=2))
    print(root)


if __name__ == '__main__':
    main()
