"""Verify BlendSpace2D authoring, typed CLI/MCP and an isolated cooked player."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


def write_json(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    with temporary.open('w', encoding='utf-8', newline='\n') as output:
        json.dump(value, output, ensure_ascii=False, indent=2)
        output.write('\n')
        output.flush()
        os.fsync(output.fileno())
    assert json.loads(temporary.read_text(encoding='utf-8')) == value
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('editor', 'cli', 'cooker', 'player', 'engine-root', 'shaderc', 'test-root'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--plugin', type=Path, action='append', default=[])
    parser.add_argument('--runtime-dll', type=Path, action='append', default=[])
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='BlendSpaceWorkflow', dir=args.test_root)).resolve()
    history = []

    def run(executable, *words, expected=0, cwd=None, input=None, timeout=300):
        command = list(map(str, [executable.resolve(), *words]))
        result = subprocess.run(command, cwd=cwd or executable.resolve().parent, input=input, capture_output=True,
                                encoding='utf-8', errors='replace', timeout=timeout)
        history.append(dict(command=command, exitCode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        write_json(root / 'commands.json', history)
        assert result.returncode == expected, history[-1]
        return result.stdout + result.stderr

    controller = '''21kb AnimatorController 3
layer "Base" "Blend" 1 18446744073709551615
state 0 1 "Blend" "/Game/Clip0.kbanim"
'''
    space = '''kbblend2d 1
sample 0 0 "/Game/Clip0.kbanim"
sample 1 0 "/Game/Clip1.kbanim"
sample 0 1 "/Game/Clip2.kbanim"
'''
    receiver = '''local configured = false
local reported = false
function Tick(self)
  if not configured then
    local applied, err = CallFunction("Animator.SetFloat", {name="X",value=0.25})
    if err or not applied then return end
    assert(CallFunction("Animator.SetFloat", {name="Y",value=0.25}))
    assert(CallFunction("Animator.SetSpeed", {speed=0}))
    assert(CallFunction("Animator.Play", {layer="Base",state="Blend",normalizedTime=0.5}))
    configured = true
    return
  end
  if reported then return end
  local position = Transform.GetPosition(self.entity)
  if math.abs(position.x - 0.5) >= 0.0001 or math.abs(position.y - 1) >= 0.0001 then return end
  Log("BlendPose:" .. position.x .. "," .. position.y)
  reported = true
end
'''
    steps = [dict(op='create_scene', name='BlendScene', template='Basic', startup=True)]
    for i, (x, y) in enumerate([(0, 0), (8, 0), (0, 8)]):
        clip = f'''21kb AnimationClip 2
durationSeconds 1
looping 1
track "." 7
key 0 0 0 0 0 0 0 0 1 1 1 1
key 0 1 {x} {y} 0 0 0 0 1 1 1 1
event 0.625 {10+i}
'''
        steps.append(dict(op='write_file', path=f'Assets/Clip{i}.kbanim', content=clip))
    for name, content in [('Motion.kbblend2d', space), ('Motion.kbanimcontroller', controller), ('Receiver.lua', receiver)]:
        steps.append(dict(op='write_file', path='Assets/' + name, content=content))
    steps += [dict(op='discover_assets'), dict(op='create_entity', id='actor', name='Actor', preset='Cube'),
              dict(op='select_entity', entity='actor'), dict(op='create_asset', id='empty', type='blend_space_2d'),
              dict(op='assert_blend_space', samples=0, dirty=False),
              dict(op='blend_space_add', x=0, y=0, clip='/Game/Clip0.kbanim'),
              dict(op='assert_blend_space', samples=1, dirty=True, position_x=0),
              dict(op='blend_space_pointer', phase='down', x=598, y=69),
              dict(op='assert_blend_space', samples=0, dirty=False), dict(op='open_asset', asset='/Game/Motion.kbblend2d'),
              dict(op='blend_space_owner', entity='actor'), dict(op='blend_space_query', x=.25, y=.25),
              dict(op='blend_space_seek', time=.5),
              dict(op='assert_blend_space', samples=3, dirty=False, phase=.5, position_x=1, position_y=1, source_position_x=0, events=0),
              dict(op='blend_space_edit', index=1, x=0, y=0, accepted=False),
              dict(op='assert_blend_space', index=1, x=1, y=0, dirty=False, position_x=1), dict(op='clear_console'),
              dict(op='blend_space_pointer', phase='down', x=559, y=371),
              dict(op='blend_space_pointer', phase='move', x=600, y=350),
              dict(op='blend_space_pointer', phase='move', x=610, y=330),
              dict(op='blend_space_pointer', phase='up'),
              dict(op='assert_blend_space', index=1, x=1.1125, y=.192, dirty=True),
              dict(op='blend_space_pointer', phase='down', x=167, y=39),
              dict(op='assert_blend_space', index=1, x=1, y=0, dirty=False, position_x=1),
              dict(op='blend_space_pointer', phase='down', x=275, y=39),
              dict(op='assert_blend_space', index=1, x=1.1125, y=.192, dirty=True),
              dict(op='blend_space_pointer', phase='down', x=167, y=39),
              dict(op='blend_space_pointer', phase='down', x=490, y=39),
              dict(op='blend_space_tick', time=.25),
              dict(op='assert_blend_space', phase=.75, position_x=1.5, position_y=1.5, events=1, event_id=10, playing=True),
              dict(op='blend_space_pointer', phase='down', x=490, y=39),
              dict(op='assert_blend_space', playing=False), dict(op='blend_space_seek', time=.5),
              dict(op='blend_space_edit', index=1, x=2),
              dict(op='assert_blend_space', position_x=.5, position_y=1, dirty=True),
              dict(op='blend_space_edit', index=1, clip='/Game/Missing.kbanim', accepted=False),
              dict(op='assert_blend_space', position_x=.5, dirty=True), dict(op='clear_console'),
              dict(op='blend_space_add', x=1, y=1, clip='/Game/Clip2.kbanim'),
              dict(op='assert_blend_space', samples=4),
              dict(op='blend_space_pointer', phase='down', x=100, y=517),
              dict(op='blend_space_pointer', phase='down', x=167, y=69),
              dict(op='assert_blend_space', samples=3),
              dict(op='blend_space_pointer', phase='down', x=598, y=69),
              dict(op='assert_blend_space', index=1, x=1, y=0, dirty=False, position_x=1),
              dict(op='blend_space_edit', index=1, x=2),
              dict(op='blend_space_pointer', phase='down', x=58, y=39),
              dict(op='assert_blend_space', dirty=False, position_x=.5, position_y=1, source_position_x=0),
              dict(op='blend_space_pointer', phase='down', x=275, y=69),
              dict(op='capture', panel='blend-space-2d', checkpoint='blend-panel', width=1100, height=720),
              dict(op='capture_blend_space_preview', checkpoint='blend-preview'),
              dict(op='attach_script', entity='actor', asset='/Game/Receiver.lua'), dict(op='save_scene'), dict(op='assert_no_errors')]
    scenario = root / 'scenario.json'
    write_json(scenario, dict(schema='21kb.editor-automation/v1', steps=steps))
    run(args.editor, '--selftest-scenario', scenario, '--selftest-root', root, '--selftest-task', 'Authoring')
    project = root / 'Authoring/workspace'
    assert (root / 'Authoring/report.json').is_file()

    def cli(*words, expected=0):
        return run(args.cli, 'blend-space', *words, '--project', project, expected=expected)

    probe = '/Game/AuthoringProbe.kbblend2d'
    assert json.loads(cli('new', '--space', probe))['samples'] == []
    for i, point in enumerate(['0,0', '1,0', '0,1']):
        cli('add', '--space', probe, '--at', point, '--clip', f'/Game/Clip{i}.kbanim')
    weights = {item['index']: item['weight'] for item in json.loads(cli('sample', '--space', probe, '--at', '.25,.25'))['weights']}
    assert weights == {0: .5, 1: .25, 2: .25}, weights
    before = (project / 'Assets/AuthoringProbe.kbblend2d').read_bytes()
    cli('set', '--space', probe, '--index', '1', '--at', '0,0', expected=1)
    assert (project / 'Assets/AuthoringProbe.kbblend2d').read_bytes() == before
    cli('new', '--space', '../../Outside.kbblend2d', expected=1)
    assert not (project.parent / 'Outside.kbblend2d').exists() and not (root / 'Outside.kbblend2d').exists()

    def mcp(name, arguments):
        request = dict(jsonrpc='2.0', id=1, method='tools/call', params=dict(name=name, arguments=arguments))
        return json.loads(run(args.cli, 'mcp', '--project', project, input=json.dumps(request) + '\n'))['result']

    sampled = mcp('blend_space', dict(action='sample', space=probe, at='.25,.25'))
    assert not sampled['isError']
    mcp_weights = {item['index']: item['weight'] for item in json.loads(sampled['content'][0]['text'])['weights']}
    assert mcp_weights == weights
    assert mcp('blend_space', dict(action='set', space=probe, index=1, at='0,0'))['isError']
    assert (project / 'Assets/AuthoringProbe.kbblend2d').read_bytes() == before
    assert mcp('blend_space', dict(action='set', space=probe, index=256, at='0,0'))['isError']
    zero = mcp('blend_space', dict(action='set', space=probe, index=0, at='0,0'))
    assert not zero['isError'], zero
    cli('assign', '--space', '/Game/Motion.kbblend2d', '--controller', '/Game/Motion.kbanimcontroller',
        '--state', 'Blend', '--x-param', 'X', '--y-param', 'Y')
    assigned = (project / 'Assets/Motion.kbanimcontroller').read_text(encoding='utf-8')
    assert assigned.startswith('21kb AnimatorController 4\n') and 'blend2D ' in assigned
    run(args.cli, 'component', 'add', '--project', project, '--entity', 'Actor', '--type', 'Animator')
    run(args.cli, 'component', 'set', '--project', project, '--entity', 'Actor', '--type', 'Animator',
        '--set', 'controller=/Game/Motion.kbanimcontroller')
    source_image = root / 'source.png'
    project_file = next(project.glob('*.21kbproject'))
    source = run(args.player, '--project=' + str(project_file), '--headless', '--frames=8', '--uncapped',
                 '--width=960', '--height=540', '--screenshot-frame=8', '--screenshot-file=' + str(source_image),
                 '--profile-fixed-step', '--profile-file=' + str(root / 'source-profile.json'))
    package = root / 'Packaged'
    package.mkdir()
    cache = args.test_root / 'CookCache'
    run(args.cooker, '--project', project, '--target', 'Windows.x64', '--output', package / 'Game.kbpack',
        '--engine-root', args.engine_root, '--shaderc', args.shaderc, '--cache', cache, timeout=600)
    player = package / 'BlendSample.exe'
    shutil.copy2(args.player, player)
    for payload in [*args.plugin, *args.runtime_dll]:
        shutil.copy2(payload, package / payload.name)
    unavailable = root / 'SourceUnavailable'
    assert project.resolve().is_relative_to(root) and unavailable.resolve().is_relative_to(root)
    project.rename(unavailable)
    shipped = run(player, '--headless', '--frames=8', '--uncapped', '--width=960', '--height=540', cwd=package)
    captured_image = root / 'packaged.png'
    captured = run(args.player, '--project=' + str(package / 'Game.kbpack'), '--headless', '--frames=8', '--uncapped',
                   '--width=960', '--height=540', '--screenshot-frame=8', '--screenshot-file=' + str(captured_image),
                   '--profile-fixed-step', '--profile-file=' + str(root / 'package-profile.json'), cwd=package)
    poses = []
    for output in [source, shipped, captured]:
        assert 'shutdown=clean' in output, output
        match = re.search(r'BlendPose:([^,\r\n]+),([^\r\n]+)', output)
        assert match, output
        pose = list(map(float, match.groups()))
        assert abs(pose[0] - .5) < 1e-4 and abs(pose[1] - 1) < 1e-4, pose
        poses.append(pose)
    sys.path.insert(0, str(args.engine_root.resolve() / 'scripts'))
    from png_readback import read_png
    assert read_png(source_image) == read_png(captured_image), 'Cooked BlendSpace2D image differs from source'
    space_file = unavailable / 'Assets/Motion.kbblend2d'
    original = space_file.read_bytes()
    space_file.write_text('kbblend2d 1\n', encoding='utf-8')
    rejected = run(args.cooker, '--project', unavailable, '--target', 'Windows.x64', '--output', root / 'Empty.kbpack',
                   '--engine-root', args.engine_root, '--shaderc', args.shaderc, '--cache', cache, expected=1, timeout=600)
    assert 'no usable samples' in rejected and not (root / 'Empty.kbpack').exists(), rejected
    space_file.write_bytes(original)
    (unavailable / 'Assets/Clip1.kbanim').unlink()
    run(args.cooker, '--project', unavailable, '--target', 'Windows.x64', '--output', root / 'Missing.kbpack',
        '--engine-root', args.engine_root, '--shaderc', args.shaderc, '--cache', cache, expected=1, timeout=600)
    assert not (root / 'Missing.kbpack').exists()
    write_json(root / 'result.json', dict(result='PASS', editorSteps=len(steps), cliMcpWeights={str(k):v for k,v in weights.items()},
        sourceUnavailable=True, shippedPlayerPassed=True, sourcePackagePoses=poses, imageDifference=0,
        emptySpaceRejected=True, missingClipRejected=True))
    print('BlendSpace2D workflow passed:', root)


if __name__ == '__main__':
    main()
