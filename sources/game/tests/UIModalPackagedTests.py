"""Verify modal focus and gameplay isolation through CLI and a cooked native player."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from UIInputPackagedTests import recording, require


LUA = r'''
local frame = 0
local valid = true
local function check(condition, message)
    if not condition then valid = false; error(message or "Modal assertion failed") end
end
local function visible(name, value)
    local ok, err = CallFunction('World.SetPropertyBool', {
        entity=World.FindByName(name), component='Canvas Group', property='visible', value=value})
    check(ok, err or 'Failed to change modal visibility')
end
function Tick(self, dt)
    frame = frame + 1
    if frame == 2 then check(UI.Focus(World.FindByName('HUD0'))) end
    if frame == 4 then visible('Dialog', true) end
    if frame == 8 then check(UI.Focused() == World.FindByName('Dialog0'), 'Dialog did not acquire focus') end
    if frame == 10 then
        check(UI.Focused() == World.FindByName('Dialog1'), 'Keyboard navigation escaped modal')
        visible('Nested', true)
    end
    if frame == 12 then
        check(UI.Focused() == World.FindByName('Nested1'), 'Controller navigation escaped nested modal')
        visible('Nested', false)
    end
    if frame == 14 then
        check(UI.Focused() == World.FindByName('Dialog1'), 'Nested modal lost previous focus')
        visible('Dialog', false)
    end
    if frame >= 5 and frame <= 18 then
        check(not Input.IsPressed('Jump'), 'Gameplay jump leaked through modal')
        check(not Input.IsPressed('Move'), 'Gameplay movement leaked through modal')
        check(not Pointer.Button(0), 'Gameplay mouse button leaked through modal')
        check(Input.TouchCount() == 0, 'Gameplay touch leaked through modal')
    end
    if frame == 16 then check(UI.Focused() == World.FindByName('HUD0'), 'HUD focus was not restored') end
    if frame == 21 then
        check(Input.IsPressed('Jump'), 'Fresh gameplay press missing after close')
        check(Pointer.Button(0), 'Fresh mouse press missing after close')
    end
    if frame == 24 then check(valid, 'Prior modal assertion failed'); Log('modal-verified') end
end
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('cli', 'cooker', 'player', 'engine-root', 'test-root', 'shaderc'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--plugin', type=Path, action='append', default=[])
    parser.add_argument('--runtime-dll', type=Path, action='append', default=[])
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='UIModalPackage', dir=args.test_root))
    project, history = root / 'Sample', []

    def run(executable, *tail, cwd=root):
        command = list(map(str, (executable, *tail)))
        result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, encoding='utf-8',
                                errors='replace', timeout=180,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        history.append(dict(command=command, code=result.returncode, stdout=result.stdout, stderr=result.stderr))
        (root / 'commands.json').write_text(json.dumps(history, indent=2), encoding='utf-8')
        require(result.returncode == 0, 'Modal command failed: ' + str(history[-1]))
        return result.stdout + result.stderr

    def component(entity, kind, *settings):
        run(args.cli, 'component', 'add', '--project', project, '--entity', entity, '--type', kind,
            *(part for setting in settings for part in ('--set', setting)))

    run(args.cli, 'new', '--parent', root, '--name', 'Sample', '--template', 'Empty')
    run(args.cli, 'entity', 'add', '--project', project, '--create', 'Camera', '--name', 'MainCamera')
    run(args.cli, 'entity', 'add', '--project', project, '--create', 'Top Down Player', '--name', 'Player')
    run(args.cli, 'entity', 'remove', '--project', project, '--name', 'Player')
    # Keep normal keyboard/controller action assets, without the movement script.
    for index, name in enumerate(('HUD', 'Dialog', 'Nested')):
        run(args.cli, 'entity', 'add', '--project', project, '--name', name)
        component(name, 'Rect Transform', 'anchorMax=1,1', 'offsetMax=0,0')
        component(name, 'Canvas', 'sortingOrder=' + str(index * 10), 'modal=' + ('true' if index else 'false'))
        component(name, 'Canvas Scaler', 'scaleMode=0', 'scaleFactor=1')
        component(name, 'Canvas Group', 'visible=' + ('false' if index else 'true'), 'transitionSeconds=0')
        for number in (0, 1):
            child = name + str(number)
            run(args.cli, 'entity', 'add', '--project', project, '--name', child, '--parent', name)
            path = name + '/' + child
            component(path, 'Rect Transform', 'offsetMin=20,' + str(20 + number * 60),
                      'offsetMax=220,' + str(60 + number * 60))
            component(path, 'Selectable')
            component(path, 'Button')
    run(args.cli, 'script-new', '--project', project, '--name', 'ModalProbe', '--language', 'lua')
    (project / 'Assets/Scripts/ModalProbe.lua').write_text(LUA, encoding='utf-8')
    run(args.cli, 'scene-attach', '--project', project, '--scene', 'Assets/Scenes/Main.21kbscene',
        '--node', 'HUD', '--script', '/Game/Scripts/ModalProbe.lua')
    frames = [{} for _ in range(24)]
    for index in range(4, 18):
        frames[index] = dict(keys=[93, 300, 200], pointer=(500, 400), analog=[(340, .8)],
                             touch=[(7, 500, 400, 0 if index == 4 else 1)])
    frames[8]['keys'].append(103)
    frames[10]['keys'].append(305)
    frames[20] = dict(keys=[93, 300, 200], pointer=(500, 400))
    replay = root / 'modal.inputrecording'
    replay.write_bytes(recording(frames))
    cli_text = run(args.cli, 'run', '--project', project, '--frames', len(frames), '--input-replay', replay)
    require('modal-verified' in cli_text, 'CLI modal acceptance did not complete: ' + cli_text)
    output = root / 'Packaged'
    output.mkdir()
    run(args.cooker, '--project', project, '--target', 'Windows.x64', '--output', output / 'Game.kbpack',
        '--engine-root', args.engine_root, '--shaderc', args.shaderc, '--cache', root / 'CookCache')
    player = output / 'Sample.exe'
    shutil.copy2(args.player, player)
    for source in [*args.plugin, *args.runtime_dll]:
        shutil.copy2(source, output / source.name)
    project.rename(root / 'SourceUnavailable')
    require('shutdown=clean' in run(player, '--headless', '--frames=3', '--uncapped',
                                   '--width=640', '--height=480', cwd=output), 'Shipped modal scene failed')
    text = run(args.player, f'--project={output / "Game.kbpack"}', '--headless', f'--frames={len(frames)}',
               '--uncapped', '--width=640', '--height=480', f'--input-replay={replay}', cwd=output)
    require('shutdown=clean' in text and 'modal-verified' in text, 'Packaged modal acceptance failed: ' + text)
    (root / 'result.json').write_text(json.dumps(dict(result='PASS', frames=len(frames), nestedModals=2,
        sourceUnavailable=True, shippedPlayerPassed=True,
        limitation='Normalized input replay verifies shared native runtime routing, not physical controller drivers.'),
        indent=2), encoding='utf-8')
    print('Packaged modal input and focus passed:', root)


if __name__ == '__main__':
    main()
