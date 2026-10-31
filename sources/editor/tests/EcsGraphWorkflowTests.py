"""Exercise graph scheduling across a dynamic module, editor and cooked player."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def write_json(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    with temporary.open('w', encoding='utf-8', newline='\n') as stream:
        json.dump(value, stream, indent=2); stream.write('\n'); stream.flush(); os.fsync(stream.fileno())
    assert json.loads(temporary.read_text(encoding='utf-8')) == value
    os.replace(temporary, path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('editor', 'cli', 'cooker', 'player', 'module', 'project-tool',
                   'engine-root', 'shaderc', 'test-root'):
        parser.add_argument('--' + option, type=Path, required=True)
    parser.add_argument('--plugin', type=Path, action='append', default=[])
    parser.add_argument('--runtime-dll', type=Path, action='append', default=[])
    args = parser.parse_args()
    for binary in (args.editor, args.cli, args.cooker, args.player, args.module,
                   args.project_tool, args.shaderc, *args.plugin, *args.runtime_dll):
        assert binary.is_file(), binary
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='EcsGraph', dir=args.test_root)).resolve()
    history = []
    environment = dict(os.environ, CMAKE_BUILD_PARALLEL_LEVEL='2')

    def run(binary, *words, expected=0, env=None, cwd=None):
        result = subprocess.run([str(binary), *map(str, words)], cwd=cwd or binary.parent,
            env=env or environment, capture_output=True, text=True, encoding='utf-8',
            errors='replace', timeout=300)
        history.append(dict(command=[str(binary), *map(str, words)], exitCode=result.returncode,
            stdout=result.stdout, stderr=result.stderr))
        write_json(root / 'commands.json', history)
        assert result.returncode == expected, history[-1]
        return result.stdout

    run(args.cli, 'new', '--parent', root, '--name', 'SampleScene', '--template', 'Basic')
    project = root / 'SampleScene'
    project_file = project / 'SampleScene.21kbproject'
    module_path = project / 'Plugins' / args.module.name
    module_path.parent.mkdir(exist_ok=True)
    shutil.copy2(args.module, module_path)
    run(args.project_tool, project_file, module_path)
    report = json.loads(run(args.cli, 'run', '--project', project, '--frames', 8,
        '--ecs-parallelism', 'combined', '--report', 'ecs'))
    scheduler = report['scheduler']
    assert report['succeeded'] and scheduler['dispatch_mode'] == 'job_graph', report
    assert any(row['name'] == 'GraphFixtureSystem' and row['chunk_jobs'] == 10
        for row in scheduler['systems']), scheduler
    assert scheduler['cpu_time_ns'] is None and scheduler['critical_path']['busy_ns'] is None
    write_json(root / 'cli-report.json', report)
    run(args.cli, 'run', '--project', project, '--ecs-parallelism', 'invalid', expected=1)

    protected = root / 'ProtectedProject'; protected.mkdir()
    descriptor = protected / 'Project.21kbproject'
    descriptor_bytes = project_file.read_bytes(); descriptor.write_bytes(descriptor_bytes)
    sentinel = protected / 'keep.txt'; sentinel.write_bytes(b'keep')
    rejected = root / 'rejected-project.json'
    for source in (descriptor, Path(str(descriptor).swapcase())):
        write_json(rejected, dict(schema='21kb.editor-automation/v1', initial_project=str(source),
            steps=[dict(op='assert_no_errors')]))
        run(args.editor, '--selftest-scenario', rejected, '--selftest-root', root,
            '--selftest-task', 'ProtectedProject', expected=1)
        assert descriptor.read_bytes() == descriptor_bytes and sentinel.read_bytes() == b'keep'

    marker = root / 'editor-frames.txt'
    scenario = root / 'scenario.json'
    steps = [dict(op='open_scene', path='Assets/Scenes/Main.21kbscene'), dict(op='play'),
        dict(op='step', frames=8, dt=1 / 60), dict(op='stop'), dict(op='assert_no_errors')]
    write_json(scenario, dict(schema='21kb.editor-automation/v1', initial_project=str(project_file), steps=steps))
    run(args.editor, '--selftest-scenario', scenario, '--selftest-root', root,
        '--selftest-task', 'Editor', env=dict(environment, KB_GRAPH_FIXTURE_MARKER=str(marker)))
    editor_report = json.loads((root / 'Editor/report.json').read_text(encoding='utf-8'))
    assert editor_report['result'] == 'PASS' and editor_report['stepsExecuted'] == len(steps)
    assert editor_report['metrics']['failures'] == 0, editor_report
    assert marker.read_text(encoding='utf-8').splitlines().count('20') >= 8

    # The cooked descriptor must locate its module independently of the source project.
    run(args.project_tool, project_file, 'Plugins/' + args.module.name)
    package = root / 'Package'; package.mkdir()
    run(args.cooker, '--project', project, '--target', 'Windows.x64',
        '--output', package / 'Game.kbpack', '--engine-root', args.engine_root,
        '--shaderc', args.shaderc, '--cache', args.test_root / 'CookCache',
        '--runtime-modules-output', package / 'RuntimeModules')
    player = package / 'GraphSample.exe'
    shutil.copy2(args.player, player)
    for dll in [*args.player.parent.glob('*.dll'), *args.plugin, *args.runtime_dll]:
        shutil.copy2(dll, package / dll.name)
    hidden = root / 'SourceUnavailable'
    assert project.resolve().is_relative_to(root) and not hidden.exists()
    marker = root / 'package-frames.txt'
    shipped_env = dict(environment, PATH=str(Path(os.environ['SystemRoot']) / 'System32'),
        KB_GRAPH_FIXTURE_MARKER=str(marker))
    project.rename(hidden)
    try:
        output = run(player, '--headless', '--frames=8', '--uncapped', cwd=package, env=shipped_env)
        assert 'shutdown=clean' in output and 'errors=0' in output and 'rendered=8' in output, output
        assert marker.read_text(encoding='utf-8').splitlines().count('20') >= 8
    finally:
        hidden.rename(project)
    write_json(root / 'result.json', dict(result='PASS', editorSteps=len(steps),
        sourceUnavailable=True, shippedPlayerPassed=True, invalidProjectPreserved=True, dispatchMode=scheduler['dispatch_mode'],
        scope='Windows graph execution across a DLL in CLI, editor Play and cooked player'))
    print('ECS graph workflow passed:', root)


if __name__ == '__main__':
    main()
