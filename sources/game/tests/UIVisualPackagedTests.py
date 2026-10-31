"""Verify cooked UI masks, effect ordering and live hidden-window resize by GPU pixels."""
from __future__ import annotations
import argparse
import csv
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'scripts'))
from png_readback import read_png


def require(value, message):
    if not value:
        raise RuntimeError(message)


def resize_hidden_player(process, sizes):
    """Resize only the test-owned, invisible native host through its normal WM_SIZE path."""
    user = ctypes.WinDLL('user32', use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    user.GetWindowDpiAwarenessContext.argtypes = [wintypes.HWND]
    user.GetWindowDpiAwarenessContext.restype = ctypes.c_void_p
    user.AreDpiAwarenessContextsEqual.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    user.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int,
                                  ctypes.c_int, ctypes.c_int, wintypes.UINT]
    handles = []
    @callback_type
    def collect(window, unused):
        pid = wintypes.DWORD()
        user.GetWindowThreadProcessId(window, ctypes.byref(pid))
        if pid.value == process.pid:
            name = ctypes.create_unicode_buffer(128)
            if user.GetClassNameW(window, name, len(name)) and name.value == '21kbGameWindow':
                handles.append(window)
        return True
    deadline = time.monotonic() + 15
    while not handles and process.poll() is None and time.monotonic() < deadline:
        user.EnumWindows(collect, 0)
        time.sleep(.01)
    require(handles, 'Hidden player window was not created')
    window = handles[0]
    require(not user.IsWindowVisible(window), 'Player unexpectedly displayed its window')
    require(user.AreDpiAwarenessContextsEqual(user.GetWindowDpiAwarenessContext(window),
                                              ctypes.c_void_p(-4)),
            'Windowed player must use per-monitor-v2 DPI awareness')
    # Wait for renderer initialization before changing dimensions.
    time.sleep(.4)
    for width, height in sizes:
        client, outer = wintypes.RECT(), wintypes.RECT()
        require(user.GetClientRect(window, ctypes.byref(client)) and
                user.GetWindowRect(window, ctypes.byref(outer)), 'Cannot inspect owned player geometry')
        extra_x = outer.right - outer.left - client.right
        extra_y = outer.bottom - outer.top - client.bottom
        require(user.SetWindowPos(window, None, 0, 0, width + extra_x, height + extra_y,
                                  0x0002 | 0x0004 | 0x0010), 'Hidden player resize failed')
        time.sleep(.25)
        require(user.GetClientRect(window, ctypes.byref(client)) and
                (client.right, client.bottom) == (width, height), 'Native client size did not change')
        require(not user.IsWindowVisible(window), 'Resize must preserve the hidden host')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('cli', 'cooker', 'player', 'engine-root', 'test-root', 'shaderc'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--plugin', type=Path, action='append', default=[])
    parser.add_argument('--runtime-dll', type=Path, action='append', default=[])
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='UIVisualPackage', dir=args.test_root))
    project = root / 'Sample'
    history = []

    def run(executable, *tail, cwd=root, resize=None):
        command = list(map(str, (executable, *tail)))
        with subprocess.Popen(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              text=True, encoding='utf-8', errors='replace',
                              creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0)) as process:
            try:
                if resize:
                    resize_hidden_player(process, resize)
                stdout, stderr = process.communicate(timeout=180)
            except BaseException:
                process.kill()
                process.communicate()
                raise
        history.append(dict(command=command, code=process.returncode, stdout=stdout, stderr=stderr))
        (root / 'commands.json').write_text(json.dumps(history, indent=2), encoding='utf-8')
        require(process.returncode == 0, 'UI package command failed: ' + str(history[-1]))
        return stdout

    def component(entity, kind, *values):
        settings = [part for value in values for part in ('--set', value)]
        run(args.cli, 'component', 'add', '--project', project, '--entity', entity, '--type', kind, *settings)

    def rect(name, parent, minimum, maximum, offsets=('0,0', '0,0')):
        run(args.cli, 'entity', 'add', '--project', project, '--name', name, '--parent', parent)
        path = parent + '/' + name
        component(path, 'Rect Transform', 'anchorMax=' + maximum, 'anchorMin=' + minimum,
                  'offsetMin=' + offsets[0], 'offsetMax=' + offsets[1])
        return path

    def border(path, color):
        component(path, 'Border', *(f'backgroundColor.{channel}={value}'
                                    for channel, value in zip('rgba', color)))

    run(args.cli, 'new', '--parent', root, '--name', 'Sample', '--template', 'Empty')
    run(args.cli, 'entity', 'add', '--project', project, '--create', 'Camera', '--name', 'MainCamera')
    # A sharp world-space edge gives the blur an independent, nonconstant input.
    material = '/Game/Materials/Backdrop.kbmat'
    run(args.cli, 'material-new', '--project', project, '--name', 'Backdrop')
    def graph(operation, *tail):
        run(args.cli, 'material-graph', operation, '--project', project, '--material', material, *tail)
    graph('setting', '--name', 'Shading Model', '--value', 'unlit')
    graph('add-node', '--kind', 'Constant', '--name', 'Bright', '--value', '1')
    graph('add-node', '--kind', 'MakeVector', '--name', 'Color')
    for channel in ('x', 'y', 'z'):
        graph('connect', '--from', 'Bright', '--to', 'Color', '--to-pin', channel)
    graph('connect', '--from', 'Color', '--to', 'Output', '--to-pin', 'emissive')
    run(args.cli, 'entity', 'add', '--project', project, '--name', 'Backdrop', '--create', 'Cube',
        '--position', '5,0,4')
    run(args.cli, 'component', 'set', '--project', project, '--entity', 'Backdrop', '--type', 'Transform',
        '--set', 'localScale.x=10', '--set', 'localScale.y=20')
    run(args.cli, 'component', 'set', '--project', project, '--entity', 'Backdrop', '--type', 'MeshRenderer',
        '--set', 'material=' + material)
    run(args.cli, 'entity', 'add', '--project', project, '--name', 'CanvasRoot')
    component('CanvasRoot', 'Rect Transform', 'anchorMax=1,1', 'offsetMax=0,0')
    component('CanvasRoot', 'Canvas')
    component('CanvasRoot', 'Canvas Scaler', 'scaleMode=0', 'scaleFactor=1')
    border('CanvasRoot', (0, 0, 0, 1))
    font_root = args.engine_root / 'sources/editor/content/EditorShell/Fonts'
    run(args.cli, 'import', '--project', project, '--destination', '/Game/Fonts', font_root / 'DejaVuSans.ttf')
    outer = rect('OuterMask', 'CanvasRoot', '.05,.05', '.45,.45')
    component(outer, 'Mask', 'showGraphic=false')
    inner = rect('InnerMask', outer, '.25,.25', '1,1', ('0,0', '200,200'))
    component(inner, 'Mask', 'showGraphic=false')
    clipped = rect('ClippedFill', inner, '0,0', '1,1', ('-200,-200', '200,200'))
    border(clipped, (1, 0, 0, 1))
    effect = rect('EffectLayers', 'CanvasRoot', '.6,.1', '.8,.3')
    border(effect, (0, 1, 0, 1))
    component(effect, 'Shadow', 'offset.x=14', 'offset.y=14', 'color.r=1', 'color.g=0',
              'color.b=0', 'color.a=1', 'blur=0')
    component(effect, 'Outline', 'color.r=0', 'color.g=0', 'color.b=1', 'color.a=1', 'width=4')
    blur = rect('BlurPanel', 'CanvasRoot', '.45,.55', '.9,.85')
    component(blur, 'Background Blur', 'radius=8')
    label = rect('LongLabel', 'CanvasRoot', '.05,.6', '.35,.7')
    component(label, 'Text', 'fontAssetId=/Game/Fonts/DejaVuSans.21kb', 'fontSize=24',
              'maxLines=1', 'overflow=2', 'wrapMode=1', 'content=' + 'Long label ' * 70)

    output = root / 'Packaged'
    output.mkdir()
    run(args.cooker, '--project', project, '--target', 'Windows.x64', '--output', output / 'Game.kbpack',
        '--engine-root', args.engine_root, '--shaderc', args.shaderc, '--cache', root / 'CookCache')
    player = output / 'Sample.exe'
    shutil.copy2(args.player, player)
    shutil.copy2(font_root / 'DejaVu-LICENSE.txt', output / 'DejaVu-LICENSE.txt')
    for source in [*args.plugin, *args.runtime_dll]:
        shutil.copy2(source, output / source.name)
    project.rename(root / 'SourceUnavailable')
    require('shutdown=clean' in run(player, '--headless', '--frames=8', '--uncapped',
                                   '--width=640', '--height=480', cwd=output), 'Shipped UI player failed')
    cases = [(640, 480, False), (960, 540, False), (640, 800, False), (1280, 960, False), (960, 540, True)]
    results = []
    for width, height, resize in cases:
        name = f'{width}x{height}' + ('-resize' if resize else '')
        picture, profile = output / (name + '.png'), output / (name + '.csv')
        tail = [f'--project={output / "Game.kbpack"}', '--headless', '--renderer=d3d11',
                f'--width={640 if resize else width}', f'--height={480 if resize else height}',
                f'--frames={180 if resize else 8}', f'--screenshot-frame={175 if resize else 6}',
                f'--screenshot-file={picture}', f'--profile-file={profile}']
        if not resize:
            tail.append('--uncapped')
        text = run(args.player, *tail, cwd=output, resize=[(640, 800), (width, height)] if resize else None)
        require('shutdown=clean' in text, name + ': player did not finish cleanly')
        rows = read_png(picture)
        require((len(rows[0]), len(rows)) == (width, height), name + ': capture dimensions mismatch')
        probes = [('mask overlap', .3 * width, .3 * height, (255, 0, 0)),
                  ('outer only clipped', .1 * width, .3 * height, (0, 0, 0)),
                  ('inner only clipped', .5 * width, .3 * height, (0, 0, 0)),
                  ('above inner clipped', .3 * width, .1 * height, (0, 0, 0)),
                  ('fill covers shadow', .7 * width, .2 * height, (0, 255, 0)),
                  ('outline covers shadow', .8 * width + 2, .2 * height, (0, 0, 255)),
                  ('shadow outside fill', .8 * width + 9, .2 * height, (255, 0, 0))]
        for label, x, y, expected in probes:
            actual = rows[int(y)][int(x)]
            require(max(abs(a-b) for a, b in zip(actual, expected)) <= 3,
                    f'{name}: {label}: {actual}, expected {expected}')
        ink = sum(min(pixel) > 80 for row in rows[int(.6 * height):int(.7 * height)]
                  for pixel in row[int(.05 * width):int(.35 * width)])
        require(ink > 30, name + ': long text rendered no visible glyphs')
        escaped = sum(max(pixel) > 5 for row in rows[int(.6 * height):int(.9 * height)]
                      for pixel in row[int(.36 * width):int(.44 * width)])
        below = sum(max(pixel) > 5 for row in rows[int(.71 * height):int(.9 * height)]
                    for pixel in row[int(.05 * width):int(.35 * width)])
        require(escaped == 0 and below == 0, name + ': truncated text escaped its layout bounds')
        blur_row = rows[int(.7 * height)]
        dark, bright = max(blur_row[int(.46 * width)]), min(blur_row[int(.6 * width)])
        transition = sum(dark + 15 < min(pixel) < bright - 15
                         for pixel in blur_row[int(.48 * width):int(.52 * width)])
        require(bright > dark + 100 and transition >= 3,
                f'{name}: background blur did not soften the world edge: {dark}, {bright}, {transition}')
        with profile.open(encoding='utf-8') as stream:
            frames = list(csv.DictReader(stream))
        require(frames and all(int(frame['missing_resources']) == 0 for frame in frames),
                name + ': rendering used missing resources')
        observed_sizes = sorted({(int(frame['width']), int(frame['height'])) for frame in frames})
        if resize:
            require((640, 800) in observed_sizes and (width, height) in observed_sizes,
                    'Runtime did not render both resized orientations')
        results.append(dict(case=name, probes=len(probes), textInk=ink, escapedTextPixels=escaped + below,
                            renderedSizes=observed_sizes, blurTransitionPixels=transition,
                            screenshot=str(picture), profile=str(profile)))
    (root / 'result.json').write_text(json.dumps(dict(result='PASS', cases=results,
        sourceUnavailable=True, shippedPlayerPassed=True,
        limitations=['Blur edge checks are not a complete reference-kernel comparison.',
                     'Frame draws and GPU timings are not a hardware overdraw counter.',
                     'Native DPI transitions and physical input hosts are separate tests.']), indent=2), encoding='utf-8')
    print('Packaged UI visual matrix passed:', root)


if __name__ == '__main__':
    main()
