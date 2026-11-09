"""Verify worker scaling, bounded telemetry and the editor ECS profiler."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('benchmark', 'editor', 'cli', 'test-root'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='EcsProfiler', dir=args.test_root)).resolve()
    commands = []

    def run(exe, *tail, fails=False):
        command = list(map(str, [exe.resolve(), *tail]))
        result = subprocess.run(command, cwd=exe.resolve().parent, capture_output=True,
                                encoding='utf-8', errors='replace', timeout=180)
        commands.append(dict(command=command, exitCode=result.returncode, expectedFailure=fails,
                             stdout=result.stdout, stderr=result.stderr))
        (root / 'commands.json').write_text(json.dumps(commands, indent=2), encoding='utf-8')
        assert (result.returncode != 0) if fails else result.returncode == 0, commands[-1]
        return result.stdout

    scaling = []
    for mode in ('uniform', 'uneven'):
        checksum = None
        for workers in (1, 2, 4):
            output = run(args.benchmark, workers, mode)
            rows = {}
            for line in output.splitlines():
                fields = line.split(',')
                rows[fields[0]] = dict(field.split('=', 1) for field in fields[1:])
            row = rows['worker_scaling']
            assert row['workers'] == str(workers) and row['correct'] == '1' and int(row['jobs']) == 21120
            if checksum is None:
                checksum = row['checksum']
            assert row['checksum'] == checksum
            growth = int(rows['settled_memory']['private_bytes']) - int(rows['warmed_memory']['private_bytes'])
            assert growth < 8 * 1024 * 1024, rows
            scaling.append(dict(workers=workers, mode=mode, metrics=rows, privateGrowthBytes=growth))
    (root / 'scaling.json').write_text(json.dumps(scaling, indent=2))

    scenario = json.loads(Path(__file__).with_name('EcsLifecycleScenario.json').read_text())
    steps = scenario['steps']
    capture = next(i for i, step in enumerate(steps) if step.get('checkpoint') == 'restored-resource')
    steps.insert(capture, dict(op='capture', panel='profiler', checkpoint='ecs-profiler', width=1000, height=600, text_fits=True))
    scenario_path = root / 'Scenario.json'
    scenario_path.write_text(json.dumps(scenario, indent=2))
    run(args.editor, '--selftest-scenario', scenario_path, '--selftest-root', root, '--selftest-task', 'GUI')
    project = root / 'GUI/workspace'
    reports = []
    for workers, frames in ((1, 32), (1, 2000), (2, 2000), (4, 2000)):
        output = run(args.cli, 'run', '--project', project, '--frames', frames, '--workers', workers,
                     '--report', 'ecs', '--ecs-history', 16)
        report = json.loads(output)
        assert report['succeeded'] and report['completed_frames'] == frames and report['worker_limit'] == workers
        assert report['history_capacity_frames'] == 16 and len(report['history']) == 16
        assert [entry['frame'] for entry in report['history']] == list(range(frames - 15, frames + 1))
        for sample in [report['telemetry'], *(entry['telemetry'] for entry in report['history'])]:
            for key, value in sample.items():
                if isinstance(value, (float, int)) and not isinstance(value, bool):
                    descriptor = sample['metric_descriptors'][key]
                    assert descriptor['unit'] and descriptor['basis'] in ('measured', 'estimated', 'derived')
        assert len(output.encode()) < 1024 * 1024
        reports.append(dict(workers=workers, frames=frames, bytes=len(output.encode()), entities=report['telemetry']['entity_count']))
        (root / f'workers-{workers}-frames-{frames}.json').write_text(output)
    assert len({row['entities'] for row in reports}) == 1
    assert max(row['bytes'] for row in reports) < reports[0]['bytes'] * 1.2
    for invalid in ('0', '65', 'bad'):
        run(args.cli, 'run', '--project', project, '--workers', invalid, fails=True)
    for invalid in ('0', '257'):
        run(args.cli, 'run', '--project', project, '--report', 'ecs', '--ecs-history', invalid, fails=True)
    run(args.cli, 'run', '--project', project, '--ecs-history', 16, fails=True)
    (root / 'result.json').write_text(json.dumps(dict(result='PASS', scaling=scaling,
        boundedHistory=reports, profilerGui=True, invalidOptionsRejected=True), indent=2))
    print(root)


if __name__ == '__main__':
    main()
