"""Verify visible gameplay graph editing, CLI/MCP authoring and source-free playback."""
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
        output.write('\n'); output.flush(); os.fsync(output.fileno())
    assert json.loads(temporary.read_text(encoding='utf-8')) == value
    temporary.replace(path)


PROBE = '''local frames = 0
function Tick(self, dt)
 frames = frames + 1
 local actor = World.FindByName("Actor")
 local position = Transform.GetPosition(actor)
 if frames == 15 or frames == 90 then
  Log(string.format('graph-position {"frame":%d,"x":%.6f}', frames, position.x))
 end
 if frames == 90 then
  assert(math.abs(position.x - 0.9) < 0.0001, "graph did not move its owner each Tick")
  Log("gameplay-graph PASS")
 end
end
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('editor', 'cli', 'cooker', 'player', 'engine-root', 'shaderc', 'test-root'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--plugin', type=Path, action='append', default=[])
    parser.add_argument('--runtime-dll', type=Path, action='append', default=[])
    args = parser.parse_args()
    for binary in (args.editor, args.cli, args.cooker, args.player, args.shaderc, *args.plugin, *args.runtime_dll):
        assert binary.is_file(), binary
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='GameplayGraph', dir=args.test_root)).resolve()
    history = []
    environment = os.environ.copy(); environment['CMAKE_BUILD_PARALLEL_LEVEL'] = '2'
    def run(executable, *words, expected=0, input=None, cwd=None):
        command = list(map(str, [executable.resolve(), *words]))
        result = subprocess.run(command, input=input, cwd=cwd or executable.resolve().parent, env=environment,
            capture_output=True, encoding='utf-8', errors='replace', timeout=600)
        history.append(dict(command=command, exitCode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        write_json(root / 'commands.json', history)
        assert result.returncode == expected, history[-1]
        return result.stdout + result.stderr
    steps = [dict(op='create_scene', name='GraphScene', template='Basic', startup=True),
        dict(op='create_entity', id='actor', name='Actor', preset='Cube'),
        dict(op='select_entity', entity='actor'), dict(op='create_asset', id='motion', type='gameplay_graph'),
        dict(op='assert_graph', nodes=1, edges=0, dirty=False),
        dict(op='graph_add', entry='Function.Transform.Translate', x=310, y=32),
        dict(op='graph_add', entry='Value:Self', x=32, y=120),
        dict(op='graph_add', entry='Literal:Float', x=32, y=240),
        dict(op='graph_literal', node=4, value='0.01'),
        dict(op='graph_add', entry='Literal:Float', x=32, y=380),
        dict(op='graph_add', entry='BuiltIn:Sequence', x=610, y=32)]
    def pointer(**fields): return dict(op='graph_pointer', phase='down', **fields)
    for source, output, target, input_pin in [(1, 'then', 2, 'exec'), (2, 'then', 6, 'exec'),
            (3, 'value', 2, 'entity'), (4, 'value', 2, 'x'), (5, 'value', 2, 'y'), (5, 'value', 2, 'z')]:
        steps += [pointer(node=source, pin=output, output=True), pointer(node=target, pin=input_pin, output=False)]
    steps += [pointer(button='Save'), dict(op='assert_graph', nodes=6, edges=6, dirty=False),
        dict(op='graph_literal', node=4, value='nan', accepted=False),
        dict(op='assert_graph', nodes=6, edges=6, dirty=False),
        dict(op='graph_connect', **{'from':6, 'out':'then', 'to':2, 'in':'exec'}, accepted=False),
        dict(op='graph_add', entry='BuiltIn:Branch', x=610, y=250),
        dict(op='graph_connect', **{'from':4, 'out':'value', 'to':7, 'in':'condition'}, accepted=False),
        pointer(button='Delete node'), pointer(button='Save'),
        pointer(node=4), dict(op='graph_pointer', phase='move', node=4, dx=40, dy=-10),
        dict(op='graph_pointer', phase='up'), dict(op='assert_graph', node=4, node_x=72, node_y=230, dirty=True),
        pointer(button='Undo'), dict(op='assert_graph', node=4, node_x=32, node_y=240, dirty=False),
        pointer(button='Redo'), dict(op='assert_graph', node=4, node_x=72, node_y=230, dirty=True),
        dict(op='reload_scene'), dict(op='select_entity', entity='actor'), dict(op='assert_graph', node=4, node_x=72, node_y=230, dirty=True),
        pointer(button='Save'), pointer(button='Revert'), dict(op='assert_graph', node=4, node_x=72, node_y=230, dirty=False),
        pointer(node=2), dict(op='graph_pointer', phase='up'), pointer(button='Breakpoint'),
        dict(op='graph_preview', entity='actor'), dict(op='graph_tick', dt=1/60),
        dict(op='assert_graph', paused=True, paused_node=2, preview_x=0, source_entity='actor', source_x=0),
        pointer(button='Step'), dict(op='assert_graph', paused=True, paused_node=6, preview_x=.01,
            node=2, pin='x', output=False, value='0.01', source_entity='actor', source_x=0),
        dict(op='assert_graph', node=2, pin='moved', output=True, value='true'),
        pointer(button='Fit'), dict(op='capture', panel='gameplay-graph', checkpoint='graph-panel', width=1180, height=760),
        pointer(button='Step'), dict(op='assert_graph', paused=False, preview_x=.01, dirty=False),
        pointer(button='Attach selected'), dict(op='write_file', path='Assets/Probe.lua', content=PROBE),
        dict(op='discover_assets'), dict(op='create_entity', id='probe', name='Probe'),
        dict(op='attach_script', entity='probe', asset='/Game/Probe.lua'), dict(op='save_scene'), dict(op='assert_no_errors')]
    scenario = root / 'scenario.json'
    write_json(scenario, dict(schema='21kb.editor-automation/v1', steps=steps))
    run(args.editor, '--selftest-scenario', scenario, '--selftest-root', root, '--selftest-task', 'Authoring')
    report = json.loads((root / 'Authoring/report.json').read_text(encoding='utf-8'))
    assert report['result'] == 'PASS' and report['stepsExecuted'] == len(steps) and report['metrics']['failures'] == 0, report
    trace = [json.loads(line) for line in (root / 'Authoring/trace.jsonl').read_text(encoding='utf-8').splitlines() if line]
    assert trace and not any(row.get('succeeded') is False for row in trace), trace[-5:]
    project = root / 'Authoring/workspace'
    assert (project / 'Assets/NewGameplayGraph.kbgraph').is_file()
    run(args.cli, 'scene-attach', '--project', project, '--node', 'Probe', '--script', '/Game/Probe.lua', '--execution-order', '100')
    def cli(*words, expected=0): return run(args.cli, 'graph', *words, '--project', project, expected=expected)
    assert json.loads(run(args.cli, 'graph', 'new', '--project', project.name, '--graph', '/Game/Relative.kbgraph', cwd=project.parent))['operation'] == 'new'
    path = '/Game/CliProbe.kbgraph'
    assert json.loads(cli('new', '--graph', path))['nodes'][0]['id'] == 1
    catalog = json.loads(cli('catalog', '--filter', 'Literal:Float'))['entries']
    assert any(entry['id'] == 'Literal:Float' for entry in catalog)
    literal = json.loads(cli('add', '--graph', path, '--entry', 'Literal:Float'))['nodeId']
    cli('literal', '--graph', path, '--node', literal, '--value', '2.5')
    cli('move', '--graph', path, '--node', literal, '--x', '-100', '--y', '240')
    before = (project / 'Assets/CliProbe.kbgraph').read_bytes()
    cli('literal', '--graph', path, '--node', literal, '--value', 'inf', expected=1)
    assert before == (project / 'Assets/CliProbe.kbgraph').read_bytes()
    cli('new', '--graph', '../../Outside.kbgraph', expected=1)
    assert not (project.parent / 'Outside.kbgraph').exists()
    assert json.loads(cli('check', '--graph', '/Game/NewGameplayGraph.kbgraph'))['valid']
    def mcp(arguments):
        request = dict(jsonrpc='2.0', id=1, method='tools/call', params=dict(name='visual_graph', arguments=arguments))
        return json.loads(run(args.cli, 'mcp', '--project', project, input=json.dumps(request) + '\n'))['result']
    changed = mcp(dict(action='literal', graph=path, node=literal, value='3.25'))
    assert not changed['isError'], changed
    assert json.loads(cli('show', '--graph', path))['nodes'][1]['value'] == '3.25'
    assert not mcp(dict(action='move', graph=path, node=literal, x=-12.5, y=400))['isError']
    assert json.loads(cli('show', '--graph', path))['nodes'][1]['x'] == -12.5
    text_node = json.loads(cli('add', '--graph', path, '--entry', 'Literal:String'))['nodeId']
    assert not mcp(dict(action='literal', graph=path, node=text_node, value=''))['isError']
    assert json.loads(cli('show', '--graph', path))['nodes'][-1]['value'] == ''
    assert mcp(dict(action='literal', graph=path, node=literal, value='nan'))['isError']
    assert mcp(dict(action='check', graph='/Game/NewGameplayGraph.kbgraph'))['isError'] is False
    sys.path.insert(0, str(args.engine_root.resolve() / 'scripts'))
    from png_readback import read_png
    def playback(content, image, frames=120):
        output = run(args.player, '--project=' + str(content), '--headless', '--frames=' + str(frames), '--uncapped',
            '--width=640', '--height=360', '--profile-fixed-step', '--profile-file=' + str(image.with_suffix('.csv')),
            '--screenshot-frame=' + str(frames), '--screenshot-file=' + str(image))
        assert 'shutdown=clean' in output and 'errors=0' in output, output
        if frames >= 90: assert 'gameplay-graph PASS' in output, output
        return output
    project_file = next(project.glob('*.21kbproject'))
    source_image, early_image = root / 'source.png', root / 'source-early.png'
    source = playback(project_file, source_image)
    playback(project_file, early_image, 15)
    assert read_png(source_image) != read_png(early_image), 'Gameplay movement did not change the rendered scene'
    package = root / 'Package'; package.mkdir()
    run(args.cooker, '--project', project, '--target', 'Windows.x64', '--output', package / 'Game.kbpack',
        '--engine-root', args.engine_root, '--shaderc', args.shaderc, '--cache', args.test_root / 'CookCache')
    player = package / 'GraphSample.exe'; shutil.copy2(args.player, player)
    for dll in [*args.plugin, *args.runtime_dll]: shutil.copy2(dll, package / dll.name)
    assert project.resolve().is_relative_to(root)
    project.rename(project.with_name('SourceUnavailable'))
    shipped = run(player, '--headless', '--frames=120', '--uncapped', '--width=640', '--height=360', cwd=package)
    assert 'gameplay-graph PASS' in shipped and 'shutdown=clean' in shipped and 'errors=0' in shipped, shipped
    captured = playback(package / 'Game.kbpack', root / 'packaged.png')
    poses = lambda text: [json.loads(value) for value in re.findall(r'graph-position (\{[^\r\n]+\})', text)]
    assert poses(source) == poses(captured) == poses(shipped)
    assert read_png(source_image) == read_png(root / 'packaged.png'), 'Cooked graph frame differs from source'
    write_json(root / 'result.json', dict(result='PASS', editorSteps=len(steps), sourceUnavailable=True,
        shippedPlayerPassed=True, sourcePackageImageDifference=0, positions=poses(source), cliMcpPassed=True,
        documentAuthoring='visible panel pointer routes and shared typed authoring API'))
    print('Gameplay graph workflow passed:', root)


if __name__ == '__main__':
    main()
