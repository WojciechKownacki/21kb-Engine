"""Check cooked atlas sampling, transformed tile picking and mixed 2D layer order."""
import argparse
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib


def png(path, width, height, pixel):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    raw = b''.join(b'\0' + bytes(c for x in range(width) for c in pixel(x, y)) for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['editor', 'cli', 'cooker', 'player', 'test-root', 'engine-root']:
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument("--plugin", type=Path, action="append", default=[])
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='MapVisual', dir=args.test_root)).resolve()
    history = []

    def run(executable, *tail):
        cmd = list(map(str, [executable, *tail]))
        r = subprocess.run(cmd, capture_output=True, encoding='utf-8', errors='replace', timeout=240, cwd=Path(executable).parent)
        history.append(dict(command=cmd, exitCode=r.returncode, stdout=r.stdout, stderr=r.stderr))
        (root / 'commands.json').write_text(json.dumps(history, indent=2), encoding='utf-8')
        assert r.returncode == 0, history[-1]
        return r.stdout + r.stderr

    steps = [dict(op='configure_runtime_rendering', width=768, height=768),
             dict(op='create_scene', name='MapVisual', template='Empty')]
    samples = []
    picks = []

    def step(op, **kw):
        steps.append(dict(op=op, **kw))

    def prop(entity, component, property, value):
        step('set_property', entity=entity, component=component, property=property, value=value)

    def image(name, width, height, pixel):
        png(root / name, width, height, pixel)
        step('copy_fixture', source=name, destination='Assets/' + name)

    colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0)]
    image('Quadrants.png', 32, 32, lambda x, y: colors[(y >= 16) * 2 + (x >= 16)])
    image('White.png', 32, 32, lambda x, y: (255, 255, 255))
    image('Tall.png', 32, 64, lambda x, y: (255, 255, 255))
    layouts = [(16, 16, 4, 2, 0, 0, 64, 32), (16, 24, 4, 2, 2, 1, 71, 61),
               (32, 16, 3, 2, 1, 2, 105, 41), (7, 11, 5, 3, 1, 3, 53, 45)]
    for i, (cw, ch, cols, rows, margin, gap, w, h) in enumerate(layouts):
        left, top = margin + (cols - 1) * (cw + gap), margin + (rows - 1) * (ch + gap)
        image(f'Atlas{i}.png', w, h, lambda x, y, l=left, t=top, a=cw, b=ch:
              (0, 255, 255) if l <= x < l + a and t <= y < t + b else (255, 0, 255))
    step('discover_assets')

    def tile(name, texture, layout, sprite, x, y, order=0, records=None, map_height=1):
        cw, ch, cols, rows, margin, gap, w, h = layout
        content = (f'21kb-map2d 2\nsize 1 {map_height}\norientation wall\nbackground false\n'
                   f'sheet {cw} {cols} {rows} "/Game/{texture}"\nlayout 0 {ch} {margin} {gap} {w} {h}\n'
                   + (records if records is not None else f't 0 0 0 g={sprite}\n'))
        step('write_file', path=f'Assets/Maps/{name}.kbmap2d', content=content)
        step('discover_assets')
        step('create_entity', id=name, name=name)
        step('add_component', entity=name, component='World2DMap')
        prop(name, 'Grid', 'wall', True)
        prop(name, 'Transform', 'localPosition.x', x)
        prop(name, 'Transform', 'localPosition.y', y)
        prop(name, 'TilemapRenderer', 'sortingOrder', order)
        step('assign_asset', entity=name, asset=f'/Game/Maps/{name}.kbmap2d', role='tilemap')

    square = (32, 32, 1, 1, 0, 0, 32, 32)
    for rotation in range(4):
        for flip in range(4):
            x, y = -3 + 2 * flip, 5 - 2 * rotation
            tint = 0xFFFFFFff if flip < 2 else 0xFFFF00ff
            name = f'Transform{rotation}{flip}'
            tile(name, 'Quadrants.png', square, f'0:0@{rotation}/{flip}/{tint}/0.2/-0.1', x - 0.2, y - 0.6)
            picks.append(dict(op='assert_scene_ray_pick', entity=name, x=x + 0.4, y=y + 0.4, z=-40))
            for u, v in [(0.25, 0.25), (0.75, 0.25), (0.25, 0.75), (0.75, 0.75)]:
                a, b = u, v
                for _ in range(rotation):
                    a, b = b, 1 - a
                if flip & 1:
                    a = 1 - a
                if flip & 2:
                    b = 1 - b
                color = list(colors[(b >= 0.5) * 2 + (a >= 0.5)])
                if flip >= 2:
                    color[2] = 0
                samples.append((x + u - 0.5, y + 0.5 - v, color, name))
    for i, layout in enumerate(layouts):
        cw, ch, cols, rows, *_ = layout
        x, y = -5 + 3 * i, -3
        tile(f'Atlas{i}', f'Atlas{i}.png', layout, f'0:{cols * rows - 1}', x, y - ch / cw / 2)
        for u in [0.04, 0.5, 0.96]:
            for v in [0.04, 0.5, 0.96]:
                samples.append((x + u - 0.5, y + (0.5 - v) * ch / cw, [0, 255, 255], f'Atlas{i}'))
    # Both directions expose mismatched sprite/map order scales, even with stable creation order.
    for i, (map_order, sprite_order) in enumerate([(1, 2), (2, 1), (-2, -1)]):
        x, y = -4 + 4 * i, -5
        tile(f'Layer{i}', 'White.png', square, '0:0@0/0/4278190335/0/0', x, y - 0.5, map_order)
        name = f'Object{i}'
        step('create_entity', id=name, name=name, preset='Sprite 2D')
        step('sprite2d_image', entity=name, asset='/Game/White.png')
        for field, value in [('pixelsPerUnit', 32), ('sortingOrder', sprite_order), ('tintR', 0), ('tintB', 0)]:
            prop(name, 'Sprite2D', field, value)
        prop(name, 'Transform', 'localPosition.x', x)
        prop(name, 'Transform', 'localPosition.y', y)
        samples.append((x, y, [0, 255, 0] if sprite_order > map_order else [255, 0, 0], name))
    tile('Floors', 'White.png', square, '0:0', -2, -7, records=
         't 0 0 0 g=0:0@0/0/16711935/0/0\nt 0 0 1 g=0:0@0/0/4278190335/0/0\n')
    samples.append((-2, -6.5, [255, 0, 0], 'Floors'))
    tile('Rows', 'Tall.png', (32,64,1,1,0,0,32,64), '0:0', 2, -7.5, map_height=2, records=
         'sortByY true\nt 0 0 0 g=0:0@0/0/4278190335/0/1\nt 0 1 0 g=0:0@0/0/16711935/0/0\n')
    samples.append((2, -6.5, [0, 255, 0], 'Rows'))
    step('create_entity', id='Camera', name='Camera', preset='Camera')
    for field, value in [('primary', True), ('projection', 1), ('orthographicHeight', 16), ('priority', 200)]:
        prop('Camera', 'Camera', field, value)
    prop('Camera', 'Transform', 'localPosition.z', -50)
    step('map2d_editor', entity='Layer0', checkpoint='layer-order-help', inputs=['settings order'])
    step('map2d_editor', entity='Layer0', checkpoint='layer-order-closed', inputs=['cancel'])
    step('save_scene')
    step('open_scene', path='Assets/Scenes/MapVisual.21kbscene')
    step('set_startup_scene', path='/Game/Scenes/MapVisual.21kbscene')
    step('play')
    step('step', frames=8)
    step('capture_runtime', checkpoint='visual', require_non_uniform=True)
    steps.extend(picks)
    step('assert_no_errors')
    step('stop')
    scenario = root / 'Scenario.json'
    scenario.write_text(json.dumps(dict(schema='21kb.editor-automation/v1', steps=steps), indent=2), encoding='utf-8')
    run(args.editor.resolve(), '--selftest-scenario', scenario, '--selftest-root', root, '--selftest-task', 'Visual')
    sys.path.insert(0, str(args.engine_root.resolve() / 'scripts'))
    from png_readback import read_png
    def verify(path):
        pixels = read_png(path)
        failures = []
        size = len(pixels)
        assert len(pixels[0]) == size
        for x, y, color, label in samples:
            px, py = int(size / 2 + x * size / 16), int(size / 2 - y * size / 16)
            actual = pixels[py][px]
            if max(abs(a - b) for a, b in zip(actual, color)) > 12:
                failures.append(dict(label=label, pixel=[px, py], expected=color, actual=actual))
        path.with_suffix('.pixels.json').write_text(json.dumps(dict(samples=len(samples), failures=failures), indent=2), encoding='utf-8')
        assert not failures, failures[:8]
    verify(root / 'Visual/automation/screenshots/visual.png')
    project = root / 'Visual/workspace'
    for path in (project / 'Assets/Maps').glob('*.kbmap2d'):
        run(args.cli.resolve(), 'map2d', 'info', '--project', project, '--map', '/Game/Maps/' + path.name)
        run(args.cli.resolve(), 'map2d', 'set', '--project', project, '--map', '/Game/Maps/' + path.name, '--size', '1x2' if path.stem == 'Rows' else '1x1')
    package = root / 'Package'
    package.mkdir()
    run(args.cooker.resolve(), '--project', project, '--target', 'Windows.x64', '--output', package / 'Game.kbpack',
        '--engine-root', args.engine_root.resolve(), '--runtime-modules-output', package / 'RuntimeModules',
        '--cache', args.test_root.resolve() / 'CookCache')
    shutil.copy2(args.player, package / args.player.name)
    for dll in [*args.player.resolve().parent.glob('*.dll'), *args.plugin]:
        shutil.copy2(dll, package / dll.name)
    hidden = root / 'SourceUnavailable'
    assert project.resolve().is_relative_to(root) and hidden.resolve().is_relative_to(root)
    project.rename(hidden)
    assert 'errors=0' in run(package / args.player.name, '--headless', '--frames=8', '--width=768', '--height=768')
    run(args.player.resolve(), '--project=' + str(package / 'Game.kbpack'), '--headless', '--frames=8',
        '--width=768', '--height=768', '--renderer=d3d11', '--profile-fixed-step',
        '--profile-file=' + str(package / 'frames.csv'), '--screenshot-file=' + str(package / 'game.png'), '--screenshot-frame=4')
    verify(package / 'game.png')
    assert 'errors=0' in run(args.player.resolve(), '--project=' + str(package / 'Game.kbpack'), '--headless', '--frames=8',
        '--width=256', '--height=256', '--renderer=d3d11', '--profile-fixed-step',
        '--profile-file=' + str(package / 'minified.csv'), '--screenshot-file=' + str(package / 'minified.png'), '--screenshot-frame=4')
    verify(package / 'minified.png')
    (root / 'result.json').write_text(json.dumps(dict(result='PASS', samples=len(samples), picks=len(picks), cooked=True, sourceUnavailable=True, captureSizes=[768,256])), encoding='utf-8')
    print(root)


if __name__ == '__main__':
    main()
