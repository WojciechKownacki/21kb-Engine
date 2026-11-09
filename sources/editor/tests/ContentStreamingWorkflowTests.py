"""Verify packaged mip/LOD streaming, upload limits and editor mesh scene transitions."""
import argparse
import json
import math
from pathlib import Path
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('renderer-tests', 'editor', 'cli', 'test-root'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='ContentStreaming', dir=args.test_root)).resolve()
    commands = []

    def run(exe, *tail):
        command = list(map(str, [exe.resolve(), *tail]))
        start = time.monotonic()
        result = subprocess.run(command, cwd=exe.resolve().parent, capture_output=True,
                                encoding='utf-8', errors='replace', timeout=180)
        commands.append(dict(command=command, exitCode=result.returncode, seconds=time.monotonic()-start,
                             stdout=result.stdout, stderr=result.stderr))
        (root / 'commands.json').write_text(json.dumps(commands, indent=2), encoding='utf-8')
        assert result.returncode == 0, commands[-1]
        return result.stdout + result.stderr

    native = run(args.renderer_tests, 'content-streaming')
    assert 'hardware streaming: cycles=4 rapid_zoom_frames=32' in native
    assert 'update_cpu_p99_ms=' in native and 'peak_upload_bytes=' in native
    run(args.cli, 'new', '--parent', root, '--name', 'Sample', '--template', 'Basic')
    project = root / 'Sample'
    model = root / 'Small.obj'
    vertices, faces = [], []
    for ring in range(65):
        phi = math.pi * ring / 64
        for segment in range(97):
            theta = 2 * math.pi * segment / 96
            vertices.append(f'v {math.sin(phi)*math.cos(theta)} {math.cos(phi)} {math.sin(phi)*math.sin(theta)}')
    for ring in range(64):
        for segment in range(96):
            a = ring * 97 + segment + 1
            b = a + 97
            faces.extend((f'f {a} {b} {a+1}', f'f {a+1} {b} {b+1}'))
    model.write_text('\n'.join(vertices + faces) + '\n', encoding='utf-8')
    run(args.cli, 'import', '--project', project, '--destination', '/Game/Models', model)
    run(args.cli, 'entity', 'add', '--project', project, '--name', 'Imported')
    run(args.cli, 'component', 'add', '--project', project, '--entity', 'Imported', '--type', 'MeshRenderer',
        '--set', 'mesh=/Game/Models/Small.21kb')
    run(args.cli, 'scene-new', '--project', project, '--name', 'Secondary', '--template', 'Basic')
    run(args.cli, 'run', '--project', project, '--frames', '4')
    scenario = root / 'scenario.json'
    steps = [dict(op='configure_runtime_rendering', width=640, height=480)]
    for i in range(16):
        name = 'Main' if i % 2 == 0 else 'Secondary'
        steps.append(dict(op='open_scene', path=f'Assets/Scenes/{name}.21kbscene'))
        steps.extend([dict(op='play'), dict(op='step', frames=8, dt=1.0/60.0),
                      dict(op='capture_runtime', checkpoint=f'transition-{i}', require_non_uniform=True),
                      dict(op='stop')])
    steps.append(dict(op='assert_no_errors'))
    scenario.write_text(json.dumps(dict(schema='21kb.editor-automation/v1', initial_project=str(next(project.glob('*.21kbproject'))), steps=steps),
                                   indent=2), encoding='utf-8')
    run(args.editor, '--selftest-scenario', scenario, '--selftest-root', root, '--selftest-task', 'GUI')
    trace = [json.loads(line) for line in (root / 'GUI/automation/trace.jsonl').read_text(encoding='utf-8').splitlines()]
    latency = sorted(float(row['detail']) for row in trace if row['operation'] == 'scene_transition_latency')
    assert len(latency) == 16 and all(row['succeeded'] for row in trace)
    result = dict(result='PASS', sceneTransitions=16,
                  sceneTransitionP95Milliseconds=latency[math.ceil(len(latency)*0.95)-1],
                  sceneTransitionP99Milliseconds=latency[math.ceil(len(latency)*0.99)-1],
                  streamingMetrics=[line for line in native.splitlines() if 'streaming' in line])
    (root / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))
    print(root)


if __name__ == '__main__':
    main()
