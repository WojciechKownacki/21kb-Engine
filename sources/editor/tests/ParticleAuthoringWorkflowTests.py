"""Verify real particle controls, shared CLI serialization and standalone export."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('editor', 'cli', 'cooker', 'player', 'shaderc', 'engine-root', 'test-root'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--plugin', type=Path, action='append', default=[])
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='ParticleAuthoring', dir=args.test_root)).resolve()
    history = []

    def run(exe, *tail):
        command = list(map(str, [exe.resolve(), *tail]))
        result = subprocess.run(command, cwd=exe.resolve().parent, capture_output=True,
                                encoding='utf-8', errors='replace', timeout=180)
        history.append(dict(command=command, exitCode=result.returncode,
                            stdout=result.stdout, stderr=result.stderr))
        (root / 'commands.json').write_text(json.dumps(history, indent=2), encoding='utf-8')
        assert result.returncode == 0, history[-1]
        return result.stdout

    scenario = root / 'scenario.json'
    scenario.write_text(json.dumps(dict(schema='21kb.editor-automation/v1',
        initial_plugins=['Rendering.21kbParticle'], steps=[
            dict(op='create_scene', name='ParticleAuthoring', template='Empty'),
            dict(op='create_asset', id='effect', type='particle_effect', folder='/Game/Vfx'),
            dict(op='open_asset', asset='effect'),
            dict(op='verify_particle_controls'),
            dict(op='create_entity', id='particles', name='Particles'),
            dict(op='add_component', entity='particles', component='Particle Effect'),
            dict(op='set_particle_effect_asset', entity='particles', asset='effect'),
            dict(op='create_entity', id='camera', name='Camera'),
            dict(op='add_component', entity='camera', component='Camera'),
            dict(op='set_entity_position', entity='camera', x=0, y=0, z=-5),
            dict(op='save_scene'),
            dict(op='set_startup_scene', path='/Game/Scenes/ParticleAuthoring.21kbscene'),
            dict(op='assert_no_errors')]), indent=2), encoding='utf-8')
    run(args.editor, '--selftest-scenario', scenario, '--selftest-root', root, '--selftest-task', 'GUI')
    project = root / 'GUI/workspace'
    effect = project / 'Assets/Vfx/NewParticleEffect.kbvfx'
    before = effect.read_text(encoding='utf-8')
    assert 'effect.emitter[0].spawn.rate.keyCount 3' in before
    assert 'effect.emitter[0].spawn.rate.key[1].value 96' in before
    assert before.strip() in run(args.cli, 'vfx-show', '--project', project,
        '--effect', '/Game/Vfx/NewParticleEffect.kbvfx')
    # Edit one curve field through the CLI; every other serialized parameter must survive.
    run(args.cli, 'vfx-set', '--project', project, '--effect', '/Game/Vfx/NewParticleEffect.kbvfx',
        '--set', 'effect.emitter[0].spawn.rate.key[1].value=72')
    after = effect.read_text(encoding='utf-8')
    assert after == before.replace('effect.emitter[0].spawn.rate.key[1].value 96',
                                    'effect.emitter[0].spawn.rate.key[1].value 72')
    blendLine = next(line for line in after.splitlines() if line.startswith('effect.emitter[0].output.blend '))
    for blend in ('Opaque', 'Alpha', 'Add', 'Multiply', 'Subtract', 'Premultiplied'):
        run(args.cli, 'vfx-set', '--project', project, '--effect', '/Game/Vfx/NewParticleEffect.kbvfx',
            '--set', 'effect.emitter[0].output.blend=' + blend)
        assert effect.read_text(encoding='utf-8') == after.replace(blendLine, 'effect.emitter[0].output.blend ' + blend)
    run(args.cli, 'vfx-set', '--project', project, '--effect', '/Game/Vfx/NewParticleEffect.kbvfx',
        '--set', 'effect.emitter[0].output.blend=' + blendLine.split()[-1])
    # Particle outputs share the same serialized controls with the editor and cooker.
    for output_type, fields in (
        ('Volumetric', {'density': '0.75', 'radiusScale': '0.625', 'lowQualitySteps': '12', 'highQualitySteps': '48'}),
        ('Decal', {'depth': '2.5'}),
        ('Trail', {'sampleIntervalSeconds': '0.125', 'minimumDistance': '0.5', 'maxSamplesPerParticle': '23', 'width': '0.75'}),
        ('Ribbon', {'maxSegments': '127', 'width': '0.625', 'breakOnDeath': 'false'}),
        ('Beam', {'localEnd': '2 3 4', 'segments': '19', 'width': '0.875', 'noiseAmplitude': '0.25', 'noiseFrequency': '1.5'}),
    ):
        effect.write_text(after, encoding='utf-8')
        edits = ['--set', 'effect.emitter[0].output.type=' + output_type]
        for field, value in fields.items():
            edits.extend(['--set', 'effect.emitter[0].output.payload.' + field + '=' + value])
        run(args.cli, 'vfx-set', '--project', project, '--effect', '/Game/Vfx/NewParticleEffect.kbvfx', *edits)
        serialized = effect.read_text(encoding='utf-8')
        assert 'effect.emitter[0].output.type ' + output_type in serialized
        for field, value in fields.items():
            assert 'effect.emitter[0].output.payload.' + field + ' ' + value in serialized
        assert serialized.strip() in run(args.cli, 'vfx-show', '--project', project,
                                         '--effect', '/Game/Vfx/NewParticleEffect.kbvfx')
        (root / (output_type + '.kbvfx')).write_text(serialized, encoding='utf-8')
    effect.write_text(after, encoding='utf-8')
    profile = json.loads(run(args.cli, 'vfx-profile', '--project', project,
        '--effect', '/Game/Vfx/NewParticleEffect.kbvfx', '--warmup', '8', '--frames', '20', '--samples', '4'))
    assert profile['succeeded'] and profile['emitters']
    assert all(row['cpu_work_profiled'] and int(row['cpu_emission_ns']) >= 0 and int(row['cpu_integration_ns']) >= 0
               for row in profile['emitters'])
    assert any(row['path'] == 'cpu' and row['cpu_render_record_count'] > 0 for row in profile['emitters'])
    (root / 'profile.json').write_text(json.dumps(profile, indent=2), encoding='utf-8')
    package = root / 'Package'
    package.mkdir()
    run(args.cooker, '--project', project, '--target', 'Windows.x64', '--output', package / 'Game.kbpack',
        '--engine-root', args.engine_root.resolve(), '--shaderc', args.shaderc.resolve(), '--cache', root / 'CookCache')
    shutil.copy2(args.player, package / args.player.name)
    for dll in [*args.player.resolve().parent.glob('*.dll'), *args.plugin]:
        shutil.copy2(dll, package / dll.name)
    hidden = root / 'SourceUnavailable'
    assert project.resolve().is_relative_to(root) and hidden.resolve().is_relative_to(root)
    project.rename(hidden)
    try:
        output = run(package / args.player.name, '--headless', '--frames=30', '--width=64', '--height=64',
                     '--particle-diagnostics', '--particle-samples=8')
        report = json.loads(next(line.split('particle_diagnostics=', 1)[1]
                                for line in output.splitlines() if line.startswith('kb_game: particle_diagnostics=')))
        (root / 'package-report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        assert report['succeeded'] and report['packaged'] and report['exclusive_package_shaders']
        assert report['rendered_frames'] == 30 and report['image']['succeeded']
        assert any(row['cpu_render_record_count'] > 0 for row in report['emitters'])
        assert 'errors=0' in output
    finally:
        hidden.rename(project)
    (root / 'result.json').write_text(json.dumps(dict(result='PASS', gui=True, cli=True,
        fullSerializedFieldParity=True, packaged=True, projectSourcesUnavailableDuringPackageRun=True), indent=2))
    print(root)


if __name__ == '__main__':
    main()
