"""Replay pointer, keyboard, controller and touch snapshots through cooked UI controls."""
from __future__ import annotations
import argparse
import json
import re
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def require(value, message):
    if not value:
        raise RuntimeError(message)


def recording(frames):
    # Public input recording v5: fixed-width little-endian fields, no struct padding.
    output = bytearray(b'21KBIRC\0' + struct.pack('<II', 5, len(frames)))
    for frame in frames:
        keys, analog, touches = frame.get('keys', []), frame.get('analog', []), frame.get('touch', [])
        output += struct.pack('<fI', .02, len(keys))
        for key in keys:
            output += struct.pack('<IB', key, 0)
        output += struct.pack('<I', len(analog))
        for key, value in analog:
            output += struct.pack('<IBf', key, 0, value)
        output += struct.pack('<I', len(touches))
        for identifier, x, y, phase in touches:
            output += struct.pack('<IffB', identifier, x, y, phase)
        output += struct.pack('<ff5B', *frame.get('pointer', (600, 450)), 1, 1, 0, 0, 0)
        text = frame.get('text', '')
        output += struct.pack('<I', len(text))
        for character in text:
            output += struct.pack('<I', ord(character))
        output += struct.pack('<II', 640, 480)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('cli', 'cooker', 'player', 'engine-root', 'test-root', 'shaderc'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--plugin', type=Path, action='append', default=[])
    parser.add_argument('--runtime-dll', type=Path, action='append', default=[])
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='UIInputPackage', dir=args.test_root))
    project, history = root / 'Sample', []

    def run(executable, *tail, expected=0, cwd=root):
        command = list(map(str, (executable, *tail)))
        result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, encoding='utf-8',
                                errors='replace', timeout=180,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        history.append(dict(command=command, code=result.returncode, stdout=result.stdout, stderr=result.stderr))
        (root / 'commands.json').write_text(json.dumps(history, indent=2), encoding='utf-8')
        require(result.returncode == expected, 'UI input command failed: ' + str(history[-1]))
        return result.stdout + result.stderr

    def component(entity, kind, *settings):
        run(args.cli, 'component', 'set' if entity == 'CanvasRoot/Choice' else 'add', '--project', project, '--entity', entity, '--type', kind,
            *(part for setting in settings for part in ('--set', setting)))

    run(args.cli, 'new', '--parent', root, '--name', 'Sample', '--template', 'Empty')
    run(args.cli, 'entity', 'add', '--project', project, '--create', 'Camera', '--name', 'MainCamera')
    run(args.cli, 'entity', 'add', '--project', project, '--name', 'CanvasRoot')
    component('CanvasRoot', 'Rect Transform', 'anchorMax=1,1', 'offsetMax=0,0')
    component('CanvasRoot', 'Canvas')
    component('CanvasRoot', 'Canvas Scaler', 'scaleMode=0', 'scaleFactor=1')
    scene_file = project / 'Assets/Scenes/Main.21kbscene'
    before = scene_file.read_bytes()
    rejected = run(args.cli, 'component', 'unknown', '--project', project, '--entity', 'CanvasRoot',
                   '--type', 'Canvas Scaler', '--set', 'scaleFactor=2', expected=1)
    require('expects add, set or remove' in rejected and scene_file.read_bytes() == before,
            'An unknown component operation must not modify the scene')
    presets = ['Canvas', 'Text', 'Image', 'Raw Image', 'Button', 'Toggle', 'Slider', 'Scrollbar',
               'Scroll View', 'Input Field', 'Dropdown', 'Progress Bar', 'Horizontal Box', 'Vertical Box',
               'Grid', 'Wrap', 'Overlay', 'Border', 'Blur', 'Widget Switcher']
    schema = run(args.cli, 'schema')
    for index, preset in enumerate(presets):
        require("'" + preset + "'" in schema, 'UI preset missing from CLI discovery: ' + preset)
        run(args.cli, 'entity', 'add', '--project', project, '--create', preset,
            '--name', 'Catalog' + str(index), '--parent', 'CanvasRoot')
    listing = run(args.cli, 'scene-list', '--project', project, '--scene', 'Assets/Scenes/Main.21kbscene')
    for index in range(len(presets)):
        require('Catalog' + str(index) + ' ' in listing, 'UI preset did not survive scene reload')
        run(args.cli, 'entity', 'remove', '--project', project, '--name', 'CanvasRoot/Catalog' + str(index))
    listing = run(args.cli, 'scene-list', '--project', project, '--scene', 'Assets/Scenes/Main.21kbscene')
    require('(2 nodes)' in listing, 'Removing UI presets left orphaned hierarchy children')
    fonts = args.engine_root / 'sources/editor/content/EditorShell/Fonts'
    run(args.cli, 'import', '--project', project, '--destination', '/Game/Fonts', fonts / 'DejaVuSans.ttf')
    widgets = [('Action', 'Button', 20), ('Check', 'Toggle', 75), ('Range', 'Slider', 130),
               ('Track', 'Scrollbar', 185), ('Entry', 'Input Field', 240), ('Choice', 'Dropdown', 295),
               ('Scroll', 'Scroll View', 350), ('Disabled', 'Button', 405)]
    for name, kind, top in widgets:
        run(args.cli, 'entity', 'add', '--project', project, '--name', name, '--parent', 'CanvasRoot',
            *(['--create', 'Dropdown'] if kind == 'Dropdown' else []))
        path = 'CanvasRoot/' + name
        component(path, 'Rect Transform', 'anchorMin=0,0', 'anchorMax=0,0', f'offsetMin=20,{top}', f'offsetMax=220,{top+40}')
        component(path, 'Border', 'backgroundColor.r=.2', 'backgroundColor.g=.2', 'backgroundColor.b=.2',
                  'backgroundColor.a=1', 'borderColor.r=1', 'borderColor.g=1', 'borderColor.b=1', 'borderColor.a=1')
        component(path, 'Selectable', 'interactable=' + ('false' if name == 'Disabled' else 'true'))
        component(path, kind, *(['optionCount=3'] if kind == 'Dropdown' else []))
        if kind == 'Input Field':
            component(path, 'Text', 'fontAssetId=/Game/Fonts/DejaVuSans.21kb', 'fontSize=20', 'content=')
        if kind == 'Scroll View':
            run(args.cli, 'entity', 'add', '--project', project, '--name', 'ScrollContent', '--parent', path)
            component(path + '/ScrollContent', 'Rect Transform', 'offsetMax=200,500')
        script_name = 'Probe' + name
        run(args.cli, 'script-new', '--project', project, '--name', script_name, '--language', 'lua')
        script = project / ('Assets/Scripts/' + script_name + '.lua')
        script.write_text('''local ticks, elapsed = 0, 0
function Tick(self, dt)
    ticks, elapsed = ticks + 1, elapsed + dt
end
''' + f'''function Destroyed(self, dt)
    Log("{name}:Timing:" .. ticks .. ":" .. elapsed)
end
''' + '\n'.join(f'''function OnUI{event}(self, event)
    Log("{name}:{event}:" .. tostring(event.args.value) .. ":" .. event.args.text .. ":" .. tostring(event.args.value2))
end''' for event in ('Clicked', 'Changed', 'Canceled', 'Submitted')), encoding='utf-8')
        run(args.cli, 'scene-attach', '--project', project, '--scene', 'Assets/Scenes/Main.21kbscene',
            '--node', name, '--script', '/Game/Scripts/' + script_name + '.lua')

    frames = [{} for _ in range(3)]
    def click(x, y):
        frames.extend([dict(pointer=(x,y)), dict(pointer=(x,y), keys=[200]), dict(pointer=(x,y))])
    def key(code):
        frames.extend([dict(keys=[code]), {}])
    click(100, 40)
    key(94)   # Enter submits the focused button.
    key(300)  # Controller primary action uses the same focused widget.
    frames.extend([dict(touch=[(7,100,40,0)]), dict(touch=[(7,100,40,2)]), {}])
    frames.extend([dict(touch=[(8,100,40,0)]), dict(touch=[(8,100,40,3)]), {}])
    frames.extend([dict(pointer=(100,40),keys=[200]), dict(pointer=(600,450)), {}])
    click(100, 425)  # Disabled button must never emit a click.
    click(100, 95)
    key(300)
    click(170, 150)
    key(105)  # ArrowRight changes the focused slider.
    click(160, 205)
    click(100, 260)
    frames.extend([dict(text='Aż😀'), dict(keys=[132,1]), {}, dict(text='Z'), {}])
    # Editing uses the same grapheme boundaries after cooking and package reload.
    unicode_edits = [('क्\u0937', ''), ('\u0600a', ''), ('ก\u0e33', ''), ('a\u200d😀', 'a\u200d')]
    for typed, remaining in unicode_edits:
        frames.extend([dict(keys=[132,1]), {}, dict(text=typed), {}])
        key(95)  # Backspace removes a whole extended grapheme.
    key(90)   # Escape must cancel through the normal input path.
    click(100, 315)
    key(103)
    key(94)
    key(300)
    key(90)
    frames.extend([dict(pointer=(100,370),analog=[(222,-1)]), {}, {}, {}])
    replay = root / 'controls.inputrecording'
    replay.write_bytes(recording(frames))
    output = root / 'Packaged'
    output.mkdir()
    run(args.cooker, '--project', project, '--target', 'Windows.x64', '--output', output / 'Game.kbpack',
        '--engine-root', args.engine_root, '--shaderc', args.shaderc, '--cache', root / 'CookCache')
    player = output / 'Sample.exe'
    shutil.copy2(args.player, player)
    shutil.copy2(fonts / 'DejaVu-LICENSE.txt', output / 'DejaVu-LICENSE.txt')
    for source in [*args.plugin, *args.runtime_dll]:
        shutil.copy2(source, output / source.name)
    project.rename(root / 'SourceUnavailable')
    require('shutdown=clean' in run(player, '--headless', '--frames=8', '--uncapped',
                                   '--width=640', '--height=480', cwd=output), 'Shipped control screen failed')
    tail = ['--headless', f'--frames={len(frames)}', '--uncapped', '--width=640', '--height=480',
            f'--input-replay={replay}']
    text = run(args.player, f'--project={output / "Game.kbpack"}', *tail, cwd=output)
    require('shutdown=clean' in text, 'Replayed package did not shut down cleanly')
    timing = re.search(r'Action:Timing:(\d+):([0-9.e+-]+)', text)
    require(timing and int(timing[1]) == len(frames) and abs(float(timing[2]) - len(frames) * .02) < .0001,
            'The packaged runtime did not use recorded frame timing')
    require(text.count('script: Action:Clicked:') == 4, 'Pointer, keyboard, controller and touch must click once each')
    require('script: Disabled:Clicked:' not in text, 'Disabled widget accepted input')
    for name in ('Check','Range','Track','Choice','Scroll'):
        require('script: ' + name + ':Changed:' in text, name + ' did not change through replayed input')
    def changed(name):
        return [float(value) for value in re.findall(name + r':Changed:([0-9.e+-]+):', text)]
    require(changed('Check') == [1.0, 0.0], 'Pointer and controller toggle values differ')
    require(len(changed('Range')) == 2 and abs(changed('Range')[0] - .75) < .001 and
            abs(changed('Range')[1] - .8) < .001, 'Slider pointer position or keyboard increment is incorrect')
    require(len(changed('Track')) == 1 and abs(changed('Track')[0] - .75) < .001,
            'Scrollbar pointer mapping is incorrect')
    require(changed('Choice') == [1.0], 'Dropdown navigation did not select the second option')
    scroll = re.search(r'Scroll:Changed:0\.0::([0-9.e+-]+)', text)
    require(scroll and abs(float(scroll[1]) - 24) < .001, 'Scroll wheel did not move content by its configured sensitivity')
    require(':Aż😀' in text and ':Z' in text and 'script: Entry:Canceled:' in text,
            'Unicode typing, selection replacement or cancel did not reach the input field')
    entry_changes = re.findall(r'script: Entry:Changed:[^:]*:(.*):[^\r\n]*', text)
    for typed, remaining in unicode_edits:
        require(any(a == typed and b == remaining for a, b in zip(entry_changes, entry_changes[1:])),
                'Packaged grapheme deletion failed for ' + repr(typed))
    # The shipped executable keeps development file-loading switches disabled.
    rejected = run(player, *tail, cwd=output, expected=1)
    require('development' in rejected and '--input-replay' in rejected, 'Shipped player accepted an input override')
    truncated = run(args.player, f'--project={output / "Game.kbpack"}', '--headless',
                    f'--frames={len(frames)+1}', f'--input-replay={replay}', expected=1)
    require('every requested frame' in truncated, 'Replay exhaustion was not refused before startup')
    replay.write_bytes(b'not a recording')
    invalid = run(args.player, f'--project={output / "Game.kbpack"}', '--headless', '--frames=1',
                  f'--input-replay={replay}', expected=1)
    require('input replay:' in invalid and 'magic' in invalid, 'Malformed replay was not diagnosed')
    (root / 'result.json').write_text(json.dumps(dict(result='PASS',frames=len(frames),
        widgets=len(widgets), cliPresets=len(presets), sourceUnavailable=True, shippedPlayerPassed=True,
        limitation='Recorded normalized inputs exercise the native runtime; this does not claim physical mobile/browser input acceptance.'),indent=2),encoding='utf-8')
    print('Packaged UI input replay passed:', root)


if __name__ == '__main__':
    main()
