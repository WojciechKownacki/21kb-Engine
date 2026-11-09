"""Replay physical platformer navigation across a gap after a live map edit."""
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
    png(root/'White.png',32,32,lambda x,y:(255,255,255))
    steps=[]
    def step(op,**kw):steps.append(dict(op=op,**kw))
    def prop(e,c,p,v):step('set_property',entity=e,component=c,property=p,value=v)
    step('configure_runtime_rendering',width=640,height=480)
    step('create_scene',name='Navigation',template='Empty')
    step('copy_fixture',source='White.png',destination='Assets/White.png')
    content='21kb-map2d 2\nsize 16 8\norientation wall\nbackground false\nsheet 32 1 1 "/Game/White.png"\n'
    for x in range(16):
        if x not in [4,5]:content+=f't {x} 7 0 s=true g=0:0\n'
    content+='t 6 6 0 s=true g=0:0\n'
    step('write_file',path='Assets/Maps/Navigation.kbmap2d',content=content)
    step('discover_assets')
    step('create_entity',id='Map',name='Map')
    for component in ['Grid','Tilemap','TilemapRenderer','TilemapCollider']:step('add_component',entity='Map',component=component)
    prop('Map','Grid','wall',True)
    step('assign_asset',entity='Map',role='tilemap',asset='/Game/Maps/Navigation.kbmap2d')
    step('create_entity',id='Navigator',name='Navigator',preset='Platformer Navigator')
    prop('Navigator','Transform','localPosition.x',-5.5)
    step('sprite2d_image',entity='Navigator',asset='/Game/White.png')
    prop('Navigator','Sprite2D','tintB',0)
    source="""
local frame,peak,previous=0,0,nil
function Tick(self,dt)
 frame=frame+1
 local hero=World.FindByName('Navigator')
 local p=CallFunction('Transform.WorldPose',{entity=hero})
 if previous then assert(math.abs(p.posX-previous)<4*dt+.1,'Navigator teleported') end
 previous=p.posX;peak=math.max(peak,p.posY)
 if frame==60 then
  assert(math.abs(p.posX+5.5)<.1,'Moved toward unreachable goal')
  assert(CallFunction('Map2D.Erase',{entity=World.FindByName('Map'),x=6,y=6,floor=0}))
 end
 if frame==299 then
  Log(string.format('navigation-final x=%.3f y=%.3f peak=%.3f',p.posX,p.posY,peak))
  assert(math.abs(p.posX+1.5)<.15,'Navigator did not reach target')
  assert(CallFunction('Physics2D.IsGrounded',{entity=hero}),'Navigator did not land')
  assert(peak>3,'Navigator did not jump the gap')
  Log('navigation-workflow-pass')
 end
end
"""
    step('write_file',path='Assets/Scripts/Observe.lua',content=source)
    step('discover_assets')
    step('create_entity',id='Observer',name='Observer')
    step('attach_script',entity='Observer',asset='/Game/Scripts/Observe.lua')
    step('save_scene')
    step('set_startup_scene',path='/Game/Scenes/Navigation.21kbscene')
    step('open_scene',path='Assets/Scenes/Navigation.21kbscene')
    step('play');step('step',frames=300,dt=.02)
    for token in ['navigation-unreachable','navigation-reached','navigation-workflow-pass']:step('assert_console',contains=token)
    step('capture_runtime',checkpoint='navigation',require_non_uniform=True)
    step('assert_no_errors');step('stop')
    scenario=root/'Scenario.json';scenario.write_text(json.dumps(dict(schema='21kb.editor-automation/v1',steps=steps),indent=2),encoding='utf-8')
    run(args.editor,'--selftest-scenario',scenario,'--selftest-root',root,'--selftest-task','Run')
    project=root/'Run/workspace'
    replay=root/'navigation.inputrecording';replay.write_bytes(recording([dict(keys=[]) for _ in range(300)]))
    assert 'navigation-workflow-pass' in run(args.cli,'run','--project',project,'--frames','300','--input-replay',replay)
    package=root/'Package';package.mkdir()
    run(args.cooker,'--project',project,'--target','Windows.x64','--output',package/'Game.kbpack','--engine-root',args.engine_root.resolve(),
        '--runtime-modules-output',package/'RuntimeModules','--cache',args.test_root.resolve()/'CookCache')
    shutil.copy2(args.player,package/args.player.name)
    for dll in [*args.player.resolve().parent.glob('*.dll'),*args.plugin]:shutil.copy2(dll,package/dll.name)
    hidden=root/'SourceUnavailable';assert project.resolve().is_relative_to(root) and hidden.resolve().is_relative_to(root);project.rename(hidden)
    assert 'errors=0' in run(package/args.player.name,'--headless','--frames=8')
    result=run(args.player,'--project='+str(package/'Game.kbpack'),'--headless','--frames=300','--uncapped','--width=640','--height=480',
        '--input-replay='+str(replay),'--screenshot-file='+str(package/'navigation.png'),'--screenshot-frame=298')
    assert 'navigation-workflow-pass' in result and 'errors=0' in result,result
    (root/'result.json').write_text(json.dumps(dict(result='PASS',sourceUnavailable=True)),encoding='utf-8');print(root)

if __name__=='__main__':main()
