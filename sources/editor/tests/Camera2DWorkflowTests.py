"""Verify camera authoring and pixel-grid rendering in both 2D orientations."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from Map2DVisualTests import png


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['editor', 'cli', 'cooker', 'player', 'test-root', 'engine-root']:
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--plugin', type=Path, action='append', default=[])
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='Camera2D', dir=args.test_root)).resolve()
    sys.path.insert(0, str(args.engine_root.resolve() / 'scripts'))
    from png_readback import read_png
    history = []

    def run(executable, *tail):
        cmd = list(map(str, [executable, *tail]))
        r = subprocess.run(cmd, capture_output=True, encoding='utf-8', errors='replace', timeout=240, cwd=Path(executable).parent)
        history.append(dict(command=cmd, exitCode=r.returncode, stdout=r.stdout, stderr=r.stderr))
        (root / 'commands.json').write_text(json.dumps(history, indent=2), encoding='utf-8')
        assert r.returncode == 0, history[-1]
        return r.stdout + r.stderr

    png(root / 'Checker.png', 16, 16, lambda x, y: (255, 0, 0) if (x+y)%2 else (0, 255, 0))
    captures = []
    def verify(path, scale):
        pixels = read_png(path)
        h, w = len(pixels), len(pixels[0])
        # Probe the interior: each checker texel occupies the same integer number of pixels.
        for vertical in [False, True]:
            colors = [pixels[h//2 + (i if vertical else 0)][w//2 + (0 if vertical else i)] for i in range(-5*scale, 5*scale)]
            labels = []
            for color in colors:
                assert color[2] < 12 and (color[0] > 240 or color[1] > 240) and min(color[:2]) < 12, (path, color)
                labels.append(color[0] > color[1])
            transitions = [i for i in range(1,len(labels)) if labels[i] != labels[i-1]]
            assert len(transitions) >= 8, (path, transitions)
            assert all(b-a == scale for a,b in zip(transitions,transitions[1:])), (path,scale,transitions)
        captures.append(str(path))

    for wall in [False, True]:
        kind = 'Wall' if wall else 'Flat'
        steps = []
        def step(op, **kw): steps.append(dict(op=op, **kw))
        def prop(entity, component, property, value): step('set_property', entity=entity, component=component, property=property, value=value)
        step('configure_runtime_rendering', width=640, height=480)
        step('create_scene', name='CameraSample', template='Empty')
        step('copy_fixture', source='Checker.png', destination='Assets/Checker.png')
        step('discover_assets')
        step('create_entity', id='Map', name='Map')
        step('add_component', entity='Map', component='World2DMap')
        step('map2d_editor', entity='Map', checkpoint='settings', width=1100, height=900,
             inputs=['view '+('wall' if wall else 'flat'), 'resize 40 30', 'settings camera', 'field 0 Hero',
                     'field 1 2', 'field 2 16', 'field 3 0', 'field 4 0', 'field 5 0', 'field 6 1'])
        step('map2d_editor', entity='Map', checkpoint='saved', inputs=['apply', 'key z ctrl', 'key y ctrl', 'key s ctrl'])
        step('assert_file', path='Assets/Maps/World.kbmap2d', contains='cameraZoom 2')
        step('create_entity', id='Hero', name='Hero')
        prop('Hero', 'Transform', 'localPosition.y' if wall else 'localPosition.z', 10 if wall else 0)
        step('create_entity', id='Checker', name='Checker', preset='Sprite 2D')
        step('sprite2d_image', entity='Checker', asset='/Game/Checker.png')
        prop('Checker', 'Sprite2D', 'pixelsPerUnit', 16)
        prop('Checker', 'Sprite2D', 'wall', wall)
        prop('Checker', 'Transform', 'localPosition.y', 10 if wall else 0.1)
        prop('Checker', 'Transform', 'localPosition.z', -0.1 if wall else 0)
        step('save_scene')
        step('set_startup_scene', path='/Game/Scenes/CameraSample.21kbscene')
        step('open_scene', path='Assets/Scenes/CameraSample.21kbscene')
        step('play')
        for i, x in enumerate([0, 0.002, 0.014, 0.02]):
            prop('Hero', 'Transform', 'localPosition.x', x)
            step('step', frames=3)
            step('assert_map2d_camera', entity='Map', orientation='wall' if wall else 'flat', height=6, x=round(x*80)/80)
            step('capture_runtime', checkpoint='pixels'+str(i), require_non_uniform=True)
        step('assert_no_errors')
        step('stop')
        scenario = root / (kind+'.json')
        scenario.write_text(json.dumps(dict(schema='21kb.editor-automation/v1', steps=steps), indent=2), encoding='utf-8')
        run(args.editor.resolve(), '--selftest-scenario', scenario, '--selftest-root', root, '--selftest-task', kind)
        for i in range(4): verify(root / kind / 'automation/screenshots' / ('pixels'+str(i)+'.png'), 5)
        captures_root = root / kind / 'automation/screenshots'
        assert read_png(captures_root/'pixels0.png') == read_png(captures_root/'pixels1.png'), 'subpixel camera movement must retain the same raster'
        project = root / kind / 'workspace'
        for zoom in ['1', '2']:
            run(args.cli.resolve(), 'map2d', 'set', '--project', project, '--map', '/Game/Maps/World.kbmap2d', '--camera-zoom', zoom)
            saved = (project/'Assets/Maps/World.kbmap2d').read_text()
            assert ('cameraZoom 2' in saved) if zoom == '2' else ('cameraZoom' not in saved)
        assert 'zoom 2' in run(args.cli.resolve(), 'map2d', 'info', '--project', project, '--map', '/Game/Maps/World.kbmap2d')
        package = root / (kind+'Package'); package.mkdir()
        run(args.cooker.resolve(), '--project', project, '--target', 'Windows.x64', '--output', package/'Game.kbpack',
            '--engine-root', args.engine_root.resolve(), '--runtime-modules-output', package/'RuntimeModules', '--cache', args.test_root.resolve()/'CookCache')
        shutil.copy2(args.player, package/args.player.name)
        for dll in [*args.player.resolve().parent.glob('*.dll'), *args.plugin]: shutil.copy2(dll, package/dll.name)
        hidden = root/(kind+'SourceUnavailable')
        assert project.resolve().is_relative_to(root) and hidden.resolve().is_relative_to(root)
        project.rename(hidden)
        assert 'errors=0' in run(package/args.player.name, '--headless', '--frames=8')
        for width,height in [(640,480),(641,481),(960,720),(800,480),(480,800)]:
            image = package / (str(width)+'x'+str(height)+'.png')
            assert 'errors=0' in run(args.player.resolve(), '--project='+str(package/'Game.kbpack'), '--headless', '--frames=8',
                '--width='+str(width), '--height='+str(height), '--renderer=d3d11', '--profile-fixed-step',
                '--profile-file='+str(image.with_suffix('.csv')), '--screenshot-file='+str(image), '--screenshot-frame=4')
            verify(image, max(1,height//88))
    (root/'result.json').write_text(json.dumps(dict(result='PASS', captures=captures, orientations=['flat','wall'])), encoding='utf-8')
    print(root)


if __name__ == '__main__':
    main()
