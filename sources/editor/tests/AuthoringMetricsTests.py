"""Verify executed authoring reports and an injected export failure."""
import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ctest', type=Path, required=True)
    parser.add_argument('--build-root', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    listing = subprocess.run([str(args.ctest), '--test-dir', str(args.build_root), '--show-only=json-v1'],
                             capture_output=True, text=True, check=True)
    tests = {test['name']: test for test in json.loads(listing.stdout)['tests']}
    workaround_ops = {'copy_fixture', 'write_file', 'touch', 'write_pcm_wave', 'copy_asset'}
    checked = []
    for name in ['kb_editor_map2d_packaged', 'kb_editor_sprite2d_packaged', 'kb_editor_headless_automation_scenario']:
        command = tests[name]['command']
        options = dict(arg[2:].split('=', 1) for arg in command if arg.startswith('-D') and '=' in arg)
        if options:
            task = options.get('TASK', 'SELFTEST-Map2D-Package')
            scenario = root / 'sources/editor/tests' / options.get('SCENARIO', 'Map2DAutomationScenario.json')
            artifacts = Path(options['TEST_ROOT']) / task
        else:
            artifacts = Path(command[command.index('--selftest-root') + 1]) / command[command.index('--selftest-task') + 1]
            scenario = Path(command[command.index('--selftest-scenario') + 1])
        steps = json.loads(scenario.read_text(encoding='utf-8'))['steps']
        report = json.loads((artifacts / 'report.json').read_text())
        assert report['result'] == 'PASS' and report['stepsExecuted'] == len(steps), name
        metrics = report['metrics']
        assertions = sum(step['op'].startswith(('assert_', 'verify_')) for step in steps)
        workarounds = [step['op'] for step in steps if step['op'] in workaround_ops]
        assert metrics['actions'] == len(steps) - assertions and metrics['assertions'] == assertions, name
        assert metrics['failures'] == 0 and metrics['workarounds'] == len(workarounds), name
        assert metrics['workaroundOperations'] == workarounds, name
        if options:
            package = max(artifacts.glob('package-*'), key=lambda p: p.stat().st_mtime)
            combined = json.loads((package / 'authoring-metrics.json').read_text())
            assert combined['result'] == 'PASS' and combined['editor'] == metrics, name
            export = combined['export']
            assert export['actions'] == 2 and export['assertions'] >= 3 and export['failures'] == 0, name
            assert export['workarounds'] == 1 and any(s['operation'] == 'assemble_player' and s['workaround'] for s in export['steps']), name
            for key in ['actions', 'assertions', 'failures', 'workarounds']:
                assert combined['total'][key] == metrics[key] + export[key], (name, key)
        checked.append(dict(test=name, metrics=metrics))
    # Fail the cooker executable after the real GUI scenario has completed.
    # The outer process must fail and the report must retain the failed action.
    command = tests['kb_editor_map2d_packaged']['command'].copy()
    command = [('-DCOOKER=' + command[0]) if arg.startswith('-DCOOKER=') else arg for arg in command]
    command = [arg for arg in command if not arg.startswith('-DTASK=')]
    command.insert(1, '-DTASK=SELFTEST-AuthoringMetrics-Failure')
    failed = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=180)
    assert failed.returncode != 0, 'Injected cooker failure must fail the harness'
    test_root = Path(next(arg.split('=', 1)[1] for arg in command if arg.startswith('-DTEST_ROOT=')))
    artifacts = test_root / 'SELFTEST-AuthoringMetrics-Failure'
    package = max(artifacts.glob('package-*'), key=lambda p: p.stat().st_mtime)
    report = json.loads((package / 'authoring-metrics.json').read_text())
    assert report['result'] == 'FAIL' and report['export']['failures'] == 1
    assert report['export']['actions'] == 1 and report['export']['steps'][0]['operation'] == 'cook'
    assert report['total']['failures'] == 1
    print(json.dumps(dict(result='PASS', scenarios=checked, injectedExportFailure=report['export']), indent=2))


if __name__ == '__main__':
    main()
