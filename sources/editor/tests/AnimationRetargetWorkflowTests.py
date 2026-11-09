"""Exercise saved retarget profiles through CLI and the headless animator editor."""
import argparse
import json
from pathlib import Path
import subprocess
import shutil
from PIL import Image, ImageChops, ImageFilter
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('editor', 'cli', 'scenario', 'test-root', 'cooker', 'player', 'shaderc', 'engine-root', 'renderer-tests'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--plugin', type=Path, action='append', default=[])
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='Retarget', dir=args.test_root)).resolve()
    history = []

    def run(exe, *tail, expected=0):
        command = list(map(str, [exe.resolve(), *tail]))
        result = subprocess.run(command, capture_output=True, encoding='utf-8', errors='replace', timeout=180)
        history.append(dict(command=command, exitCode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        (root / 'commands.json').write_text(json.dumps(history, indent=2), encoding='utf-8')
        assert result.returncode == expected, history[-1]
        return result.stdout

    run(args.editor, '--selftest-scenario', args.scenario.resolve(), '--selftest-root', root, '--selftest-task', 'GUI')
    project = root / 'GUI/workspace'
    target = project / 'Assets/Skeletal/Target.kbskeleton'
    target.write_text('21kb Skeleton 1\nbone 10 -1 "Root" 0 2 0 0 0 0.707106781 0.707106781 1 1 1\n'
                      'inverseBind 0 -1 0 0 1 0 0 0 0 0 1 0 -2 0 0 1\n', encoding='utf-8')
    common = ['animator', 'retarget', '--project', project, '--clip', '/Game/Animation/Move.kbanim',
              '--character', '/Game/Skeletal/Target.kbskeleton']
    profile = root / 'Mapping.kbretarget'
    run(args.cli, *common, '--name', 'Retargeted', '--save-profile', profile)
    run(args.cli, *common, '--name', 'RetargetedReload', '--profile', profile)
    clip = project / 'Assets/Animation/Retargeted.kbanim'
    reloaded = project / 'Assets/Animation/RetargetedReload.kbanim'
    assert clip.read_bytes() == reloaded.read_bytes(), 'saved mapping changed exported animation'
    assert 'skeletalTrack 10 ' in clip.read_text(encoding='utf-8')
    stale = root / 'Stale.kbretarget'
    fields = profile.read_text(encoding='utf-8').splitlines()
    signatures = fields[1].split()
    signatures[0] = str(int(signatures[0]) ^ 1)
    fields[1] = ' '.join(signatures)
    stale.write_text('\n'.join(fields) + '\n', encoding='utf-8')
    run(args.cli, *common, '--name', 'Rejected', '--profile', stale, expected=1)
    assert not (project / 'Assets/Animation/Rejected.kbanim').exists()
    run(args.cli, *common, '--name', 'Retargeted', '--profile', profile, expected=1)
    run(args.cli, 'animator', 'new', '--project', project, '--name', 'RetargetPlayback',
        '--clip', '/Game/Animation/Retargeted.kbanim')
    run(args.cli, 'animator', 'check', '--project', project,
        '--controller', '/Game/Animation/RetargetPlayback.kbanimcontroller', '--character', '/Game/Skeletal/Target.kbskeleton')
    binding = next(line.split()[1:] for line in clip.read_text(encoding='utf-8').splitlines()
                   if line.startswith('targetSkeleton '))
    mesh = project / 'Assets/Skeletal/Target.kbskeletalmesh'
    mesh.write_text('21kb SkeletalMesh 1\nskeleton ' + ' '.join(binding) + '\n'
        'bounds 0 0 0 1 1 0.1\nlod 1\n'
        'vertex 0 -0.5 -0.5 0 0 0 -1 1 0 0 1 0 0 0 0 0 0 1 0 0 0\n'
        'vertex 0 0.5 -0.5 0 0 0 -1 1 0 0 1 1 0 0 0 0 0 1 0 0 0\n'
        'vertex 0 0 0.5 0 0 0 -1 1 0 0 1 0.5 1 0 0 0 0 1 0 0 0\n'
        'index 0 0\nindex 0 2\nindex 0 1\nsection 0 0 3 0\nsectionBone 0 0 10\nrequiredBone 0 10\n', encoding='utf-8')
    run(args.cli, 'scene-new', '--project', project, '--name', 'RetargetExport', '--template', 'Empty', '--default', 'yes')
    run(args.cli, 'entity', 'add', '--project', project, '--name', 'Character')
    run(args.cli, 'component', 'add', '--project', project, '--entity', 'Character', '--type', 'MeshRenderer')
    run(args.cli, 'component', 'set', '--project', project, '--entity', 'Character', '--type', 'MeshRenderer',
        '--set', 'mesh=/Game/Skeletal/Target.kbskeletalmesh')
    run(args.cli, 'component', 'add', '--project', project, '--entity', 'Character', '--type', 'Animator')
    run(args.cli, 'component', 'set', '--project', project, '--entity', 'Character', '--type', 'Animator',
        '--set', 'controller=/Game/Animation/RetargetPlayback.kbanimcontroller')
    run(args.cli, 'entity', 'add', '--project', project, '--name', 'Camera', '--create', 'Camera', '--position', '0.5,0,-4')
    run(args.cli, 'entity', 'add', '--project', project, '--create', 'Directional Light', '--rotation', '0,0,0')
    run(args.cli, 'entity', 'add', '--project', project, '--create', 'Environment')
    package = root / 'Package'
    package.mkdir()
    run(args.cooker, '--project', project, '--target', 'Windows.x64', '--output', package / 'Game.kbpack',
        '--engine-root', args.engine_root.resolve(), '--shaderc', args.shaderc.resolve(), '--cache', root / 'CookCache')
    shutil.copy2(args.player, package / args.player.name)
    for dll in [*args.player.resolve().parent.glob('*.dll'), *args.plugin]:
        shutil.copy2(dll, package / dll.name)
    run(args.renderer_tests, 'retarget-export-capture', project, root / 'source')
    original = [Image.open(root / ('source' + str(frame) + '.ppm')).convert('RGB') for frame in (1, 30)]
    modes = ('taa', 'reset', 'fxaa', 'ortho-taa', 'ortho-reset', 'ortho-fxaa')
    for mode in modes:
        run(args.renderer_tests, 'temporal-export-capture', project, root / ('source-' + mode), mode)
    hidden = root / 'SourceUnavailable'
    project.rename(hidden)
    try:
        run(args.renderer_tests, 'retarget-export-capture', package / 'Game.kbpack', root / 'package')
        exported = [Image.open(root / ('package' + str(frame) + '.ppm')).convert('RGB') for frame in (1, 30)]
        for mode in modes:
            run(args.renderer_tests, 'temporal-export-capture', package / 'Game.kbpack', root / ('package-' + mode), mode)
            for frame in range(1, 31):
                source = Image.open(root / ('source-' + mode + str(frame) + '.ppm')).convert('RGB')
                cooked = Image.open(root / ('package-' + mode + str(frame) + '.ppm')).convert('RGB')
                assert ImageChops.difference(source, cooked).getbbox() is None, ('temporal export mismatch', mode, frame)
        assert 'errors=0' in run(package / args.player.name, '--headless', '--frames=30', '--width=256', '--height=256')
    finally:
        hidden.rename(project)
    for source_image, package_image in zip(original, exported):
        assert ImageChops.difference(source_image, package_image).getbbox() is None, 'cook changed rendered skinning'
    difference = ImageChops.difference(exported[0], exported[1])
    changed = sum(max(pixel) > 8 for pixel in difference.getdata())
    assert changed > 100, ('retargeted mesh did not visibly animate', changed)
    temporal_metrics = []
    for projection in ('', 'ortho-'):
        worst_flat_error = 0
        for frame in range(1, 31):
            taa = Image.open(root / ('package-' + projection + 'taa' + str(frame) + '.ppm')).convert('RGB')
            reset = Image.open(root / ('package-' + projection + 'reset' + str(frame) + '.ppm')).convert('RGB')
            difference = ImageChops.difference(taa, reset)
            neighborhood = ImageChops.difference(reset.filter(ImageFilter.MaxFilter(7)), reset.filter(ImageFilter.MinFilter(7)))
            flat_error = max((max(error) for error, extent in zip(difference.getdata(), neighborhood.getdata()) if max(extent) <= 3), default=0)
            worst_flat_error = max(worst_flat_error, flat_error)
            assert flat_error <= 4, ('history trails outside current edges', projection, frame, flat_error)
            if frame in (1, 16):
                assert difference.getbbox() is None, ('camera cut retained history', projection, frame)
        temporal_metrics.append(dict(projection=projection or 'perspective', maxFlatRegionError=worst_flat_error))
    (root / 'temporal-result.json').write_text(json.dumps(dict(result='PASS', framesPerMode=30,
        modes=modes, sourcePackagePixelParity=True, movingSkinnedMesh=True, movingParticles=True,
        particleBlendModes=['Alpha', 'Add', 'Premultiplied'], cameraCutFrame=16, metrics=temporal_metrics), indent=2))
    (root / 'result.json').write_text(json.dumps(dict(result='PASS', gui=True, cli=True,
        savedMappingPreservesMotion=True, staleProfileRejected=True, packagedPixelParity=True, animatedPixels=changed), indent=2))
    print(root)


if __name__ == '__main__':
    main()
