"""Compare ECS storage paths and exercise resource ownership through authoring."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('benchmark', 'resource-tests', 'editor', 'cli', 'compiler', 'engine-root', 'test-root'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='EcsStorage', dir=args.test_root)).resolve()
    commands = []

    def run(exe, *tail):
        command = list(map(str, [exe.resolve(), *tail]))
        result = subprocess.run(command, cwd=root, capture_output=True, encoding='utf-8', errors='replace', timeout=180)
        commands.append(dict(command=command, exitCode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        (root / 'commands.json').write_text(json.dumps(commands, indent=2), encoding='utf-8')
        assert result.returncode == 0, commands[-1]
        return result.stdout

    resource_result = run(args.resource_tests)
    assert 'created=2 destroyed=2' in resource_result and 'fresh-backend-events' in resource_result
    assert 'backend-events-disabled' in resource_result
    measurements = []
    for count in (20000, 100000, 200000):
        expected = None
        for mode, options in (('full', ['direct-add']), ('native-only', ['native-only', 'direct-add']),
                              ('flecs-reference', ['flecs-reference'])):
            for repetition in range(3):
                output = run(args.benchmark, count, 60, *options)
                rows = {}
                for line in output.splitlines():
                    fields = line.split(',')
                    if '=' not in fields[0]:
                        rows[fields[0]] = dict(field.split('=', 1) for field in fields[1:])
                result = rows['result']
                assert result['entities'] == result['active'] == str(count)
                if expected is None:
                    expected = result
                assert result == expected, (mode, count, result, expected)
                assert int(rows['resident_memory']['private_bytes']) > 0
                assert int(rows['resident_memory']['working_set_bytes']) > 0
                assert int(rows['bulk_create_allocations']['flecs_c_allocator_calls']) > 0
                measurements.append(dict(mode=mode, entities=count, repetition=repetition, metrics=rows))
    (root / 'measurements.json').write_text(json.dumps(measurements, indent=2))

    # Compile a valid control before rejecting a nontrivial hot-column type, so
    # missing compiler headers or environment cannot masquerade as a rejection.
    environment = args.compiler.resolve().parents[6] / 'Auxiliary/Build/vcvars64.bat'
    assert environment.is_file(), environment
    for kind, member, succeeds in [('valid', 'unsigned value;', True), ('invalid', 'std::string value;', False)]:
        source = root / (kind + '.cpp')
        source.write_text('#include "engine/ecs/World.hpp"\n#include <string>\n'
                          'struct Component {' + member + '};\n'
                          'void Probe(kb::ecs::World& world) { (void)world.RegisterComponent<Component>(); }\n')
        command_file = root / (kind + '.cmd')
        command_file.write_text('@echo off\ncall "' + str(environment) + '" >nul\n'
            + '"' + str(args.compiler.resolve()) + '" /nologo /std:c++20 /EHsc /c '
            + '/I"' + str(args.engine_root.resolve() / 'sources/engine/include') + '" '
            + '/I"' + str(args.engine_root.resolve() / 'third_party/flecs/include') + '" '
            + '"' + str(source) + '" /Fo"' + str(root / (kind + '.obj')) + '"\n')
        compiled = subprocess.run(['cmd', '/c', str(command_file)], cwd=root, capture_output=True, timeout=60)
        output = (compiled.stdout + compiled.stderr).decode('utf-8', errors='replace')
        (root / (kind + '-compile.log')).write_text(output, encoding='utf-8')
        assert (compiled.returncode == 0) == succeeds, output
        if not succeeds:
            assert 'ECS component must be trivially copyable' in output, output

    run(args.editor, '--selftest-scenario', Path(__file__).with_name('EcsLifecycleScenario.json').resolve(),
        '--selftest-root', root, '--selftest-task', 'GUI')
    project = root / 'GUI/workspace'
    report = json.loads(run(args.cli, 'run', '--project', project, '--frames', 16, '--report', 'ecs'))
    assert report['succeeded'] and report['completed_frames'] == 16
    assert report['storage_path']['storage'] == 'native-archetypes'
    assert report['storage_path']['mirror_entities'] and report['storage_path']['mirror_components']
    (root / 'cli-report.json').write_text(json.dumps(report, indent=2))
    (root / 'result.json').write_text(json.dumps(dict(result='PASS', workloads=27,
        allEntityResultsEqual=True, resourceOwnership=resource_result, nontrivialTypeRejected=True,
        guiCloneMigrationDeleteUndoReload=True, cliReport=True), indent=2))
    print(root)


if __name__ == '__main__':
    main()
