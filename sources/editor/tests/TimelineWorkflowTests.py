"""Verify Timeline authoring, isolated preview and cooked player behavior."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('editor', 'engine-tests', 'cooker', 'player', 'engine-root', 'shaderc', 'test-root'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--plugin', type=Path, action='append', default=[])
    parser.add_argument('--runtime-dll', type=Path, action='append', default=[])
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='TimelineWorkflow', dir=args.test_root)).resolve()
    history = []

    def run(exe, *tail, cwd=None, expected=0, timeout=300):
        result = subprocess.run(list(map(str, [exe.resolve(), *tail])), cwd=cwd or exe.resolve().parent,
                                capture_output=True, encoding='utf-8', errors='replace', timeout=timeout)
        history.append(dict(command=list(map(str, [exe, *tail])), exitCode=result.returncode,
                            stdout=result.stdout, stderr=result.stderr))
        (root / 'commands.json').write_text(json.dumps(history, indent=2), encoding='utf-8')
        assert result.returncode == expected, history[-1]
        return result.stdout + result.stderr

    child = '''kbtimeline 2
duration 1
binding "Receiver" "."
event "Receiver" 0.375 3 "Nested" "nested payload"
'''
    sequence = '''kbtimeline 2
duration 1
binding "Target" "."
binding "Actor" "Animated"
transform "Target" 0 0 0 0 0 0 0 1 1 1 1
transform "Target" 1 2 0 0 0 0 0 1 1 1 1
event "Target" 0.25 1 "First" "one"
event "Target" 0.75 2 "Last" "two"
'''
    clip = '''21kb AnimationClip 2
durationSeconds 1
looping 0
track "." 7
key 0 0 0 0 0 0 0 0 1 1 1 1
key 0 1 0 2 0 0 0 0 1 1 1 1
event 0.625 9
'''
    receiver = '''function Created(self)
  SetShared("count", 0)
  Events.Subscribe("OnTimelineEvent", function(event)
    SetShared("count", GetShared("count") + 1)
    SetShared("last", event.args.name)
    Log("TimelineEvent:" .. event.args.name .. ":" .. event.args.payload)
  end)
  Events.Subscribe("OnAnimationEvent", function(event)
    Log("TimelineAnimation:" .. event.args.event)
  end)
end
'''
    # The final player starts the same saved sequence on the same authored owner.
    # The preview drives its own instance before this startup callback is added.
    final_receiver = '-- @asset /Game/Sequence.kbtimeline\n' + receiver + '''
local started = false
function Tick(self)
  if started then return end
  started = true
  local instance, err = Timeline.Create("/Game/Sequence.kbtimeline")
  assert(not err and instance ~= 0)
  assert(Timeline.Skip(instance, 1, true))
  assert(Timeline.Seek(instance, 0.5))
  Log("TimelineReady")
end
'''
    steps = [dict(op='create_scene', name='Timeline', template='Basic', startup=True)]
    actor_receiver = '''function Created(self)
  Events.Subscribe("OnAnimationEvent", function(event)
    Log("TimelineAnimation:" .. event.args.event)
  end)
end
'''
    for name, content in [('Sequence.kbtimeline', sequence), ('Child.kbtimeline', child), ('Motion.kbanim', clip), ('Receiver.lua', receiver), ('ActorReceiver.lua', actor_receiver)]:
        steps.append(dict(op='write_file', path='Assets/' + name, content=content))
    steps += [dict(op='write_pcm_wave', path='Assets/Tone.wav', duration_ms=2000, sample_rate=48000),
              dict(op='discover_assets'), dict(op='create_entity', id='target', name='Target', preset='Cube'),
              dict(op='add_component', entity='target', component='Light'),
              dict(op='create_entity', id='actor', name='Animated', preset='Cube'),
              dict(op='reparent_entity', entity='actor', parent='target'),
              dict(op='attach_script', entity='actor', asset='/Game/ActorReceiver.lua'),
              dict(op='attach_script', entity='target', asset='/Game/Receiver.lua'),
              dict(op='select_entity', entity='target'), dict(op='open_asset', asset='/Game/Sequence.kbtimeline'),
              dict(op='timeline_owner', entity='target'), dict(op='timeline_seek', time=.5),
              dict(op='assert_timeline', time=.5, position_x=1, events=0, dirty=False, playing=False),
              dict(op='assert_timeline', source_position_x=0),
              dict(op='timeline_edit', label='Duration (seconds)', text='0', accepted=False),
              dict(op='assert_timeline', time=.5, position_x=1, dirty=False), dict(op='clear_console'),
              dict(op='timeline_edit', label='    Position (x y z)', occurrence=1, text='4 0 0'),
              dict(op='assert_timeline', time=.5, position_x=2, dirty=True),
              dict(op='timeline_pointer', x=110, y=15),  # Undo
              dict(op='assert_timeline', time=.5, position_x=1, dirty=False),
              dict(op='timeline_pointer', x=205, y=15),  # Redo
              dict(op='assert_timeline', time=.5, position_x=2, dirty=True),
              dict(op='timeline_add', kind='property', binding='Target', reference='Light.intensity'),
              dict(op='timeline_edit', label='    Value', text='2.5'),
              dict(op='timeline_add', kind='audio', binding='Target', reference='/Game/Tone.wav'),
              dict(op='timeline_add', kind='sequence', binding='Target', reference='/Game/Child.kbtimeline'),
              dict(op='timeline_add', kind='animation', binding='Actor', reference='/Game/Motion.kbanim'),
              dict(op='timeline_pointer', x=25, y=15),  # Save
              dict(op='assert_timeline', dirty=False),
              dict(op='timeline_seek', time=0), dict(op='timeline_pointer', x=400, y=15),  # Play
              dict(op='timeline_tick', time=.5), dict(op='assert_timeline', time=.5, position_x=2, events=2, last='Nested', playing=True),
              dict(op='timeline_pointer', x=500, y=15),  # Pause
              dict(op='assert_timeline', playing=False),
              dict(op='timeline_seek', time=1), dict(op='timeline_pointer', x=590, y=15),  # Reverse
              dict(op='timeline_tick', time=.5), dict(op='assert_timeline', time=.5, events=3, last='Last'),
              dict(op='timeline_pointer', x=500, y=15), dict(op='timeline_seek', time=.9),
              dict(op='timeline_pointer', x=25, y=45), dict(op='timeline_pointer', x=400, y=15),
              dict(op='timeline_tick', time=.2), dict(op='assert_timeline', time=.1, position_x=.4, events=3),
              dict(op='timeline_tick', time=.3), dict(op='assert_timeline', time=.4, events=5, last='Nested'),
              dict(op='timeline_pointer', x=500, y=15), dict(op='timeline_pointer', x=25, y=45),
              dict(op='timeline_seek', time=.5),
              dict(op='capture', panel='timeline', checkpoint='timeline-panel', width=1100, height=720),
              dict(op='capture_timeline_preview', checkpoint='timeline-preview'),
              dict(op='timeline_edit', label='    Value', text='3.5'),
              dict(op='timeline_pointer', x=590, y=45),  # Revert: omitted below to avoid a modal dirty prompt.
              ]
    steps.pop()
    steps += [dict(op='timeline_pointer', x=110, y=15), dict(op='assert_timeline', dirty=False),
              dict(op='save_scene'),
              dict(op='write_file', path='Assets/Receiver.lua', content=final_receiver), dict(op='discover_assets'),
              dict(op='assert_no_errors')]
    scenario = root / 'scenario.json'
    scenario.write_text(json.dumps(dict(schema='21kb.editor-automation/v1', steps=steps)), encoding='utf-8')
    run(args.editor, '--selftest-scenario', scenario, '--selftest-root', root, '--selftest-task', 'Authoring')
    project = root / 'Authoring/workspace'
    assert (root / 'Authoring/report.json').is_file(), 'Editor workflow did not produce its report'
    source_pcm = run(args.engine_tests, 'timeline-project', project)
    project_file = next(project.glob('*.21kbproject'))
    source_image = root / 'source.png'
    source = run(args.player, '--project=' + str(project_file), '--headless', '--frames=8', '--uncapped',
                 '--width=960', '--height=540', '--screenshot-frame=8', '--screenshot-file=' + str(source_image),
                 '--profile-fixed-step', '--profile-file=' + str(root / 'source-profile.json'))
    output = root / 'Packaged'
    output.mkdir()
    run(args.cooker, '--project', project, '--target', 'Windows.x64', '--output', output / 'Game.kbpack',
        '--engine-root', args.engine_root, '--shaderc', args.shaderc, '--cache', args.test_root / 'CookCache', timeout=600)
    player = output / 'TimelineSample.exe'
    shutil.copy2(args.player, player)
    for source_file in [*args.plugin, *args.runtime_dll]:
        shutil.copy2(source_file, output / source_file.name)
    project.rename(root / 'SourceUnavailable')
    packaged_pcm = run(args.engine_tests, 'timeline-project', output / 'Game.kbpack', cwd=output)
    package_image = root / 'packaged.png'
    packaged = run(player, '--headless', '--frames=8', '--uncapped', '--width=960', '--height=540', cwd=output)
    captured = run(args.player, '--project=' + str(output / 'Game.kbpack'), '--headless', '--frames=8', '--uncapped',
                   '--width=960', '--height=540', '--screenshot-frame=8', '--screenshot-file=' + str(package_image),
                   '--profile-fixed-step', '--profile-file=' + str(root / 'package-profile.json'), cwd=output)
    for label, text in [('source', source), ('package', packaged), ('captured package', captured)]:
        assert 'shutdown=clean' in text and 'TimelineReady' in text, (label, text)
        assert all('TimelineEvent:' + name in text for name in ('First', 'Nested', 'Last')), (label, text)
        assert 'TimelineAnimation:9' in text, (label, text)
    events = lambda text: re.findall(r'script: (Timeline(?:Event|Animation):[^\r\n]+)', text)
    assert events(source) == events(packaged), 'Source and packaged event order/payload differs'
    assert events(source) == events(captured), 'Captured package event order/payload differs'
    pcm = lambda text: list(map(float, re.search(r'TimelinePCM ([^\r\n]+)', text)[1].split()))
    assert all(abs(a-b) < 1e-6 for a, b in zip(pcm(source_pcm), pcm(packaged_pcm))), 'Cooked audio differs from source'
    sys.path.insert(0, str(args.engine_root.resolve() / 'scripts'))
    from png_readback import read_png
    assert read_png(source_image) == read_png(package_image), 'Saved source and packaged Timeline images differ'
    # A missing Timeline dependency must fail cooking without publishing an output.
    restored = root / 'SourceUnavailable'
    (restored / 'Assets/Tone.wav').unlink()
    run(args.cooker, '--project', restored, '--target', 'Windows.x64', '--output', root / 'Missing.kbpack',
        '--engine-root', args.engine_root, '--shaderc', args.shaderc, '--cache', args.test_root / 'CookCache', expected=1, timeout=600)
    assert not (root / 'Missing.kbpack').exists(), 'Missing Timeline audio dependency produced a package'
    (root / 'result.json').write_text(json.dumps(dict(result='PASS', editorSteps=len(steps),
        sourceUnavailable=True, shippedPlayerPassed=True, pcmSource=pcm(source_pcm), pcmPackaged=pcm(packaged_pcm),
        events=events(packaged), imageDifference=0, missingDependencyRejected=True), indent=2), encoding='utf-8')
    print('Timeline workflow passed:', root)


if __name__ == '__main__':
    main()
