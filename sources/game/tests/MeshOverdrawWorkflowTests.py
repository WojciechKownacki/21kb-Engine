"""Exercise scoped mesh overdraw on a real headless game renderer."""
import argparse
import json
from pathlib import Path
import statistics
import subprocess
import tempfile

from PIL import Image, ImageChops


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('editor', 'game', 'test-root'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='MeshOverdraw', dir=args.test_root)).resolve()

    def run(exe, *tail):
        result = subprocess.run([str(exe.resolve()), *map(str, tail)], cwd=exe.resolve().parent,
                                capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=180)
        if result.returncode:
            raise RuntimeError(f'{exe.name} exited {result.returncode}: {result.stdout}\n{result.stderr}')

    def create_project(copies):
        steps = [{'op': 'create_scene', 'name': 'Coverage', 'template': 'Basic'},
                 {'op': 'create_entity', 'id': 'cube', 'name': 'Cube', 'preset': 'Cube'}]
        steps += [{'op': 'duplicate_entity', 'entity': 'cube', 'id': 'copy' + str(i)} for i in range(copies)]
        steps += [{'op': 'save_scene'}, {'op': 'set_startup_scene', 'path': '/Game/Scenes/Coverage.21kbscene'},
                  {'op': 'assert_no_errors'}]
        scenario = root / f'scenario-{copies}.json'
        scenario.write_text(json.dumps({'schema': '21kb.editor-automation/v1', 'steps': steps}), encoding='utf-8')
        run(args.editor, '--selftest-scenario', scenario, '--selftest-root', root, '--selftest-task', str(copies))
        return root / str(copies) / 'workspace'

    def sample(project, name, enabled):
        profile, image = root / (name + '.csv'), root / (name + '.png')
        flags = ['--profile-scene-overdraw'] if enabled else []
        run(args.game, '--project=' + str(project), '--headless', '--frames=60', '--profile-fixed-step',
            '--profile-file=' + str(profile), '--screenshot-file=' + str(image), '--screenshot-frame=60', *flags)
        traces = [json.loads(line) for line in Path(str(profile) + '.trace.jsonl').read_text(encoding='utf-8').splitlines()]
        assert len(traces) == 60
        results = [frame['rendering']['mesh_overdraw'] for frame in traces]
        values = [item['passing_samples'] for item in results if item['available']]
        if enabled:
            assert len(values) >= 10 and min(values) > 0
        else:
            assert not values and all(not item['enabled'] for item in results)
        return traces, values, image

    baseline, layered = create_project(0), create_project(4)
    plain, _, plain_image = sample(baseline, 'plain', False)
    measured, single_values, measured_image = sample(baseline, 'measured', True)
    _, layered_values, _ = sample(layered, 'layered', True)
    assert [row['rendering']['draw_count'] for row in plain] == [row['rendering']['draw_count'] for row in measured]
    difference = ImageChops.difference(Image.open(plain_image).convert('RGB'), Image.open(measured_image).convert('RGB'))
    assert all(high <= 1 for _, high in difference.getextrema())
    assert statistics.median(layered_values) > statistics.median(single_values)
    particle_scenario = root / 'particle-scenario.json'
    particle_scenario.write_text(json.dumps({'schema': '21kb.editor-automation/v1',
        'initial_plugins': ['Rendering.21kbParticle'], 'steps': [
            {'op': 'create_scene', 'name': 'Particles', 'template': 'Empty'},
            {'op': 'create_asset', 'id': 'effect', 'type': 'particle_effect', 'folder': '/Game/Vfx'},
            {'op': 'create_entity', 'id': 'particles', 'name': 'Particles'},
            {'op': 'add_component', 'entity': 'particles', 'component': 'Particle Effect'},
            {'op': 'set_particle_effect_asset', 'entity': 'particles', 'asset': 'effect'},
            {'op': 'create_entity', 'id': 'camera', 'name': 'Camera'},
            {'op': 'add_component', 'entity': 'camera', 'component': 'Camera'},
            {'op': 'set_entity_position', 'entity': 'camera', 'x': 0, 'y': 0, 'z': -5},
            {'op': 'save_scene'}, {'op': 'set_startup_scene', 'path': '/Game/Scenes/Particles.21kbscene'},
            {'op': 'assert_no_errors'}]}), encoding='utf-8')
    run(args.editor, '--selftest-scenario', particle_scenario, '--selftest-root', root, '--selftest-task', 'particles')
    particle_profile = root / 'particles.csv'
    particle_image = root / 'particles-measured.png'
    particle_plain = root / 'particles-plain.png'
    particle_project = '--project=' + str(root / 'particles/workspace')
    run(args.game, particle_project, '--headless', '--frames=120', '--profile-fixed-step',
        '--profile-file=' + str(root / 'particles-plain.csv'),
        '--screenshot-file=' + str(particle_plain), '--screenshot-frame=120')
    run(args.game, particle_project, '--headless', '--frames=120', '--profile-fixed-step',
        '--profile-scene-overdraw', '--profile-file=' + str(particle_profile),
        '--screenshot-file=' + str(particle_image), '--screenshot-frame=120')
    particle_traces = [json.loads(line) for line in Path(str(particle_profile) + '.trace.jsonl').read_text(encoding='utf-8').splitlines()]
    assert any(frame['rendering']['particle_overdraw']['draws'] > 0 and
               (frame['rendering']['particle_overdraw']['passing_samples'] or 0) > 0 for frame in particle_traces)
    difference = ImageChops.difference(Image.open(particle_plain).convert('RGB'),
                                       Image.open(particle_image).convert('RGB'))
    assert all(high <= 1 for _, high in difference.getextrema())
    (root / 'result.json').write_text(json.dumps({'result': 'PASS', 'single_samples': statistics.median(single_values),
        'layered_samples': statistics.median(layered_values)}, indent=2), encoding='utf-8')
    print(root)


if __name__ == '__main__':
    main()
