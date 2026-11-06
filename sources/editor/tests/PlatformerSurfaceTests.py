"""Replay the platformer preset on slopes, one-way and moving platforms."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from Map2DVisualTests import png


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ['editor','cli','cooker','player','test-root','engine-root']:
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--plugin',type=Path,action='append',default=[])
    args=parser.parse_args();args.test_root.mkdir(parents=True,exist_ok=True)
    root=Path(tempfile.mkdtemp(prefix='PlatformNavigation',dir=args.test_root)).resolve()
    sys.path.insert(0,str(args.engine_root.resolve()/'sources/game/tests'))
    from UIInputPackagedTests import recording
    history=[]
    def run(exe,*tail):
        cmd=list(map(str,[Path(exe).resolve(),*tail]))
        r=subprocess.run(cmd,cwd=Path(exe).resolve().parent,capture_output=True,encoding='utf-8',errors='replace',timeout=240)
        history.append(dict(command=cmd,exitCode=r.returncode,stdout=r.stdout,stderr=r.stderr))
        (root/'commands.json').write_text(json.dumps(history,indent=2),encoding='utf-8')
        assert r.returncode==0,history[-1]
        return r.stdout+r.stderr
    for kind in ['Slope','OneWay','Moving']:
        folder=root/kind;folder.mkdir()
        png(folder/'White.png',32,32,lambda x,y:(255,255,255))
        steps=[]
        def step(op,**kw):steps.append(dict(op=op,**kw))
        def prop(e,c,p,v):step('set_property',entity=e,component=c,property=p,value=v)
        step('configure_runtime_rendering',width=640,height=480)
        step('create_scene',name='Surface',template='Empty')
        step('copy_fixture',source='White.png',destination='Assets/White.png')
        step('discover_assets')
        step('create_entity',id='Player',name='Player',preset='Platformer Player')
        step('sprite2d_image',entity='Player',asset='/Game/White.png')
        prop('Player','Sprite2D','tintB',0)
        if kind=='Slope':prop('Player','Transform','localPosition.x',-4)
        def box(name,x,y,width,height,angle=0,moving=False):
            step('create_entity',id=name,name=name,preset='Sprite 2D')
            step('sprite2d_image',entity=name,asset='/Game/White.png')
            prop(name,'Transform','localPosition.x',x);prop(name,'Transform','localPosition.y',y)
            # Collider geometry owns its angle/size; the marker remains a simple reference sprite.
            step('add_component',entity=name,component='Collider2D')
            prop(name,'Collider2D','width',width);prop(name,'Collider2D','height',height)
            prop(name,'Collider2D','angleDegrees',angle)
            if moving:
                step('add_component',entity=name,component='Body2D');prop(name,'Body2D','type',1)
        if kind=='Slope':
            box('Floor',0,-.5,40,1)
            box('Ramp',6,1.2,12,.5,25)
        elif kind=='Moving':box('Platform',0,0,8,.5,moving=True)
        else:
            content='21kb-map2d 2\nsize 8 8\norientation wall\nbackground false\nsheet 32 1 1 "/Game/White.png"\n'
            for x in range(8):content+=f't {x} 7 0 s=true g=0:0\n'
            for x in range(2,6):content+=f't {x} 4 0 g=0:0\ncollision {x} 4 0 4 false 0.5 0\n'
            step('write_file',path='Assets/Maps/Surface.kbmap2d',content=content)
            step('discover_assets');step('create_entity',id='Map',name='Map')
            for component in ['Grid','Tilemap','TilemapRenderer','TilemapCollider']:step('add_component',entity='Map',component=component)
            prop('Map','Grid','wall',True)
            step('assign_asset',entity='Map',role='tilemap',asset='/Game/Maps/Surface.kbmap2d')
        source="""
local frame,peak,takeoff=0,0,nil
local kind='KIND'
function Tick(self,dt)
 frame=frame+1
 local hero=World.FindByName('Player')
 local p=CallFunction('Transform.WorldPose',{entity=hero});peak=math.max(peak,p.posY)
 if kind=='Moving' then
  if frame==60 then assert(CallFunction('Physics2D.SetVelocity',{entity=World.FindByName('Platform'),x=2,y=.5})) end
  if frame==120 then
   local support=CallFunction('Transform.WorldPose',{entity=World.FindByName('Platform')})
   assert(math.abs(p.posX-support.posX)<.1 and p.posX>2,'Rider did not follow moving platform')
   assert(CallFunction('Physics2D.IsGrounded',{entity=hero}),'Rider lost moving platform')
  end
  if frame==130 then takeoff=p.posX end
 end
 if frame==179 then
  Log(string.format('surface-final KIND x=%.3f y=%.3f peak=%.3f',p.posX,p.posY,peak))
  if kind=='Slope' then assert(p.posX>3 and peak>2.5,'Preset did not climb slope') end
  if kind=='OneWay' then assert(p.posY>4.7 and p.posY<4.95 and CallFunction('Physics2D.IsGrounded',{entity=hero}),'Preset did not pass through and land on one-way platform') end
  if kind=='Moving' then assert(takeoff and math.abs(p.posX-takeoff)<.15 and peak>4,'Jump did not detach rider') end
  Log('surface-workflow-pass')
 end
end
""".replace('KIND',kind)
        step('write_file',path='Assets/Scripts/Observe.lua',content=source)
        step('discover_assets');step('create_entity',id='Observer',name='Observer')
        step('attach_script',entity='Observer',asset='/Game/Scripts/Observe.lua')
        step('save_scene');step('set_startup_scene',path='/Game/Scenes/Surface.21kbscene')
        step('open_scene',path='Assets/Scenes/Surface.21kbscene');step('play')
        frames=[];previous=set()
        for i in range(180):
            held=set()
            if kind=='Slope' and 60<=i<160:held.add('D')
            if (kind=='OneWay' and i==80) or (kind=='Moving' and i==125):held.add('Space')
            for key in previous-held:step('key',key=key,down=False)
            for key in held-previous:step('key',key=key,down=True)
            step('step',frames=1,dt=.02);previous=held
            frames.append(dict(keys=[4 if key=='D' else 93 for key in held]))
        step('assert_console',contains='surface-workflow-pass')
        step('capture_runtime',checkpoint='surface',require_non_uniform=True);step('assert_no_errors');step('stop')
        scenario=folder/'Scenario.json';scenario.write_text(json.dumps(dict(schema='21kb.editor-automation/v1',steps=steps),indent=2),encoding='utf-8')
        run(args.editor,'--selftest-scenario',scenario,'--selftest-root',folder,'--selftest-task','Run')
        project=folder/'Run/workspace';replay=folder/'surface.inputrecording';replay.write_bytes(recording(frames))
        assert 'surface-workflow-pass' in run(args.cli,'run','--project',project,'--frames','180','--input-replay',replay)
        package=folder/'Package';package.mkdir()
        run(args.cooker,'--project',project,'--target','Windows.x64','--output',package/'Game.kbpack','--engine-root',args.engine_root.resolve(),
            '--runtime-modules-output',package/'RuntimeModules','--cache',args.test_root.resolve()/'CookCache')
        shutil.copy2(args.player,package/args.player.name)
        for dll in [*args.player.resolve().parent.glob('*.dll'),*args.plugin]:shutil.copy2(dll,package/dll.name)
        hidden=folder/'SourceUnavailable';assert project.resolve().is_relative_to(root) and hidden.resolve().is_relative_to(root);project.rename(hidden)
        assert 'errors=0' in run(package/args.player.name,'--headless','--frames=8')
        result=run(args.player,'--project='+str(package/'Game.kbpack'),'--headless','--frames=180','--uncapped','--width=640','--height=480',
            '--input-replay='+str(replay),'--screenshot-file='+str(package/'surface.png'),'--screenshot-frame=178')
        assert 'surface-workflow-pass' in result and 'errors=0' in result,result
    (root/'result.json').write_text(json.dumps(dict(result='PASS',sourceUnavailable=True,cases=['Slope','OneWay','Moving'])),encoding='utf-8');print(root)

if __name__=='__main__':main()
