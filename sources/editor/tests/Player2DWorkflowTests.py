"""Create, play, round-trip and package platformer and top-down component workflows."""
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
    parser.add_argument('--tile-collisions', action='store_true')
    parser.add_argument('--beginner', action='store_true')
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='Player2D', dir=args.test_root)).resolve()
    sys.path.insert(0, str(args.engine_root.resolve() / 'sources/game/tests'))
    from UIInputPackagedTests import recording
    sys.path.insert(0, str(args.engine_root.resolve() / 'scripts'))
    from png_readback import read_png
    history = []

    def run(exe, *tail):
        cmd = list(map(str, [Path(exe).resolve(), *tail]))
        result = subprocess.run(cmd, cwd=Path(exe).resolve().parent, capture_output=True, encoding='utf-8', errors='replace', timeout=600)
        history.append(dict(command=cmd, exitCode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        (root / 'commands.json').write_text(json.dumps(history, indent=2), encoding='utf-8')
        assert result.returncode == 0, history[-1]
        return result.stdout + result.stderr

    for platformer in [True, False]:
        mode = 'Platformer' if platformer else 'TopDown'
        folder = root / mode
        folder.mkdir()
        png(folder / 'White.png', 32, 32, lambda x, y: (255,255,255))
        initial_project = None
        if args.beginner:
            run(args.cli, 'new', '--parent', folder, '--name', 'SampleProject', '--template', 'Empty')
            initial_project = next((folder/'SampleProject').glob('*.21kbproject'))
        steps = []
        def step(op, **kw): steps.append(dict(op=op, **kw))
        def prop(entity, component, property, value):
            step('set_property', entity=entity, component=component, property=property, value=value)
        step('configure_runtime_rendering', width=640, height=480)
        step('create_scene', name='Sample', template='Empty')
        image_asset = '/Game/White.png'
        if args.beginner:
            step('create_entity', id='MapRoot', selected_id='PaintedMap', name='PaintedMap', preset='Map Platformer' if platformer else 'Map Top Down')
            step('map2d_editor', entity='PaintedMap', checkpoint='import-and-paint', width=1100, height=700,
                 inputs=['import '+str(folder/'White.png'), 'click 45 70', 'click 25 185',
                         'drag 560 280 720 280', 'remember-map', 'key z ctrl', 'key y ctrl',
                         'assert-map-unchanged', 'key s ctrl'])
            step('assert_file', path='Assets/Maps/World 2.kbmap2d', contains='g=')
            image_asset = '/Game/2D Map Sprites/Imported/White.png'
        else:
            step('copy_fixture', source='White.png', destination='Assets/White.png')
        step('discover_assets')
        step('create_entity', id='Player', preset='Platformer Player' if platformer else 'Top Down Player')
        step('sprite2d_image', entity='Player', asset=image_asset)
        prop('Player', 'Sprite2D', 'tintB', 0)
        step('assert_component', entity='Player', component='CharacterController2D')
        step('assert_component', entity='Player', component='CharacterController', exists=False)
        step('assert_property', entity='Player', component='CharacterController2D', property='sideView', value=platformer)
        step('component_inspector_field',entity='Player',component='CharacterController2D',property='radius',text='0.36')
        step('assert_property',entity='Player',component='CharacterController2D',property='radius',value=.36)
        step('undo')
        step('assert_property',entity='Player',component='CharacterController2D',property='radius',value=.35 if platformer else .3)
        step('component_inspector_field',entity='Player',component='Sprite2D',property='flipX')
        step('assert_property',entity='Player',component='Sprite2D',property='flipX',value=True)
        step('undo')
        step('create_prefab', entity='Player', path='Assets/Prefabs/Player.kbprefab')
        step('instantiate_prefab', id='Copy', name='Copy', path='Assets/Prefabs/Player.kbprefab', virtual_path='/Game/Prefabs/Player.kbprefab')
        step('assert_component', entity='Copy', component='CharacterController2D')
        step('assert_component', entity='Copy', component='Sprite2D')
        step('delete_entity', entity='Copy')
        def box(name, x, y, z, sx, sy, sz, solid, green=False):
            step('create_entity', id=name, name=name, preset='Sprite 2D')
            step('sprite2d_image', entity=name, asset=image_asset)
            prop(name, 'Sprite2D', 'pixelsPerUnit', 32)
            prop(name, 'Sprite2D', 'wall', platformer)
            if green:
                prop(name, 'Sprite2D', 'tintR', 0)
                prop(name, 'Sprite2D', 'tintB', 0)
            for field, value in [('localPosition.x',x),('localPosition.y',y),('localPosition.z',z),('localScale.x',sx),('localScale.y',sy),('localScale.z',sz)]:
                prop(name,'Transform',field,value)
            if solid and not args.tile_collisions:
                step('add_component', entity=name, component='Collider2D')
                prop(name,'Collider2D','sideView',platformer)
        if platformer: box('Floor',0,-.5,0,30,1,1,True)
        box('Obstacle',2,.5 if platformer else .5,0,1,1,1 if platformer else 3,True)
        box('Goal',5,1 if platformer else .5,0,1,1,1,False,True)
        if args.tile_collisions:
            cells = [(x,7) for x in range(30)] + [(17,6)] if platformer else [(17,y) for y in range(2,5)]
            content = '21kb-map2d 2\nsize 30 8\nbackground false\norientation ' + ('wall' if platformer else 'flat') + '\n'
            content += ''.join(f't {x} {y} 0 s=true\n' for x,y in cells)
            step('write_file',path='Assets/Maps/Collision.kbmap2d',content=content)
            step('discover_assets')
            step('create_entity',id='TileCollision',name='TileCollision')
            for component in ['Grid','Tilemap','TilemapRenderer','TilemapCollider']:
                step('add_component',entity='TileCollision',component=component)
            prop('TileCollision','Grid','wall',platformer)
            prop('TileCollision','Transform','localPosition.x',-.5)
            prop('TileCollision','Transform','localPosition.y',-1 if platformer else .5)
            if not platformer: prop('TileCollision','Transform','localPosition.z',-.5)
            step('assign_asset',entity='TileCollision',asset='/Game/Maps/Collision.kbmap2d',role='tilemap')

        step('create_entity',id='HUD',name='HUD')
        step('add_component',entity='HUD',component='preset:Canvas')
        step('create_entity',id='Status',name='Status')
        step('reparent_entity',entity='Status',parent='HUD')
        step('add_component',entity='Status',component='preset:Text')
        prop('Status','Text','content','Reach the green goal')
        prop('Status','Rect Transform','offsetMin.x',20)
        prop('Status','Rect Transform','offsetMin.y',20)
        prop('Status','Rect Transform','offsetMax.x',400)
        prop('Status','Rect Transform','offsetMax.y',60)
        source = """
local frame, won, peak = 0, false, 0
local platformer = PLATFORMER
function Tick(self, dt)
 frame = frame + 1
 local player = World.FindByName('Player')
 local p = CallFunction('Transform.WorldPose', {entity=player})
 peak = math.max(peak,p.posY)
 if frame == (platformer and 78 or 24) then
  assert(p.posX > .5 and p.posX < 1.4, 'Obstacle did not stop the player')
 end
 if math.abs(p.posX-5) < .8 and (platformer or math.abs(p.posZ) < .7) and not won then
  won = true
  assert(CallFunction('World.SetPropertyString', {entity=World.FindByName('Status'),component='Text',property='content',value='Goal reached'}))
  Log('goal-reached')
 end
 if frame == 179 then
  Log(string.format('player-final %.3f %.3f %.3f peak %.3f',p.posX,p.posY,p.posZ,peak))
  assert(won, 'Goal was not reached')
  if platformer then assert(peak > 2.5, 'Jump did not clear the obstacle') end
  Log('player-workflow-pass')
 end
end
""".replace('PLATFORMER',str(platformer).lower())
        step('write_file',path='Assets/Scripts/Goal.lua',content=source)
        step('discover_assets')
        step('attach_script',entity='HUD',asset='/Game/Scripts/Goal.lua')
        step('save_scene')
        step('set_startup_scene',path='/Game/Scenes/Sample.21kbscene')
        step('open_scene',path='Assets/Scenes/Sample.21kbscene')
        step('assert_component',entity='Player',component='CharacterController2D')
        step('select_entity',entity='Player')
        step('set_inspector_scroll',position='top')
        step('capture',panel='inspector',checkpoint='player-components',width=480,height=1800)
        step('play')
        frames=[]
        previous=set()
        keys={'D':4,'W':23,'S':19,'Space':93}
        for i in range(180):
            held=set()
            if platformer:
                if 60 <= i < 125: held.add('D')
                if i == 80: held.add('Space')
            else:
                if i < 25 or 60 <= i < 100: held.add('D')
                if 25 <= i < 60: held.add('W')
                if 100 <= i < 135: held.add('S')
            for key in sorted(previous-held): step('key',key=key,down=False)
            for key in sorted(held-previous): step('key',key=key,down=True)
            step('step',frames=1,dt=.02)
            frames.append(dict(keys=[keys[k] for k in sorted(held)]))
            previous=held
        step('assert_console',contains='player-workflow-pass')
        step('assert_property',entity='Status',component='Text',property='content',value='Goal reached')
        step('assert_sprite_visibility',entity='Player',visible=True)
        step('capture_runtime',checkpoint='game',require_non_uniform=True)
        step('assert_no_errors')
        step('stop')
        if args.beginner:
            step('build_game', output=str(folder/'Export'), build_root=str(args.player.resolve().parent.parent), builder=sys.executable)
        scenario=folder/'Scenario.json'
        scenario_data = dict(schema='21kb.editor-automation/v1',steps=steps)
        if initial_project: scenario_data['initial_project'] = str(initial_project)
        scenario.write_text(json.dumps(scenario_data,indent=2))
        run(args.editor,'--selftest-scenario',scenario,'--selftest-root',folder,'--selftest-task','Run')
        project=folder/'Run/workspace'
        def cli(*tail): return run(args.cli,*tail,'--project',project)
        preset_probe=folder/'PresetProbe'
        shutil.copytree(project,preset_probe)
        run(args.cli,'entity','add','--project',preset_probe,'--create','Platformer Player' if platformer else 'Top Down Player','--name','Spare')
        info=run(args.cli,'scene-list','--project',preset_probe)
        spare = next(line for line in info.splitlines() if '] Spare components:' in line)
        assert 'CharacterController2D' in spare and 'Sprite2D' in spare,info
        replay=folder/'play.inputrecording'
        replay.write_bytes(recording(frames))
        assert 'player-workflow-pass' in cli('run','--frames','180','--input-replay',replay)
        package=folder/'Package'
        package.mkdir()
        run(args.cooker,'--project',project,'--target','Windows.x64','--output',package/'Game.kbpack',
            '--engine-root',args.engine_root.resolve(),'--runtime-modules-output',package/'RuntimeModules','--cache',root/'CookCache')
        shutil.copy2(args.player,package/args.player.name)
        for dll in [*args.player.resolve().parent.glob('*.dll'),*args.plugin]: shutil.copy2(dll,package/dll.name)
        hidden=folder/'SourceUnavailable'
        assert project.resolve().is_relative_to(root) and hidden.resolve().is_relative_to(root)
        project.rename(hidden)
        assert 'errors=0' in run(package/args.player.name,'--headless','--frames=8')
        if args.beginner:
            exported = folder/'Export/Sample-Windows.x64-Development'
            assert 'errors=0' in run(exported/'Sample.exe','--headless','--frames=8')
            exported_run = run(args.player,'--project='+str(exported/'Game.kbpack'),'--headless','--frames=180','--uncapped',
                               '--input-replay='+str(replay))
            assert 'player-workflow-pass' in exported_run and 'errors=0' in exported_run, exported_run
        output=run(args.player,'--project='+str(package/'Game.kbpack'),'--headless','--frames=180','--uncapped',
            '--width=640','--height=480','--input-replay='+str(replay),'--screenshot-file='+str(package/'game.png'),'--screenshot-frame=178')
        assert 'player-workflow-pass' in output and 'errors=0' in output,output
        for image in [folder/'Run/automation/screenshots/game.png',package/'game.png']:
            pixels=[pixel for row in read_png(image) for pixel in row]
            player_pixels=sum(r>200 and g>200 and b<50 for r,g,b,*_ in pixels)
            goal_pixels=sum(r<50 and g>200 and b<50 for r,g,b,*_ in pixels)
            assert player_pixels>24 and goal_pixels>24, (image,player_pixels,goal_pixels)
    (root/'result.json').write_text(json.dumps(dict(result='PASS',modes=['Platformer','TopDown'],sourceUnavailable=True,tileCollisions=args.tile_collisions,beginner=args.beginner)))
    print(root)

if __name__ == '__main__': main()
