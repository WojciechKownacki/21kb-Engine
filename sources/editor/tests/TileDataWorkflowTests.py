"""Verify typed tile schemas, migration and budgeted prefab streaming."""
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
    root=Path(tempfile.mkdtemp(prefix='TileData',dir=args.test_root)).resolve()
    sys.path.insert(0,str(args.engine_root.resolve()/'sources/game/tests'))
    from UIInputPackagedTests import recording
    history=[]
    def run(exe,*tail,expected=0):
        cmd=list(map(str,[Path(exe).resolve(),*tail]))
        r=subprocess.run(cmd,cwd=Path(exe).resolve().parent,capture_output=True,encoding='utf-8',errors='replace',timeout=240)
        history.append(dict(command=cmd,exitCode=r.returncode,stdout=r.stdout,stderr=r.stderr))
        (root/'commands.json').write_text(json.dumps(history,indent=2),encoding='utf-8')
        assert r.returncode==expected,history[-1]
        return r.stdout+r.stderr
    png(root/'White.png',32,32,lambda x,y:(255,255,255))
    steps=[]
    def step(op,**kw):steps.append(dict(op=op,**kw))
    def prop(e,c,p,v):step('set_property',entity=e,component=c,property=p,value=v)
    step('configure_runtime_rendering',width=640,height=480)
    step('create_scene',name='TileData',template='Empty')
    step('copy_fixture',source='White.png',destination='Assets/White.png');step('discover_assets')
    step('create_entity',id='Marker',name='Marker',preset='Sprite 2D')
    step('sprite2d_image',entity='Marker',asset='/Game/White.png')
    prop('Marker','Sprite2D','wall',False);prop('Marker','Sprite2D','tintB',0)
    step('create_entity',id='Child',name='MarkerChild');step('reparent_entity',entity='Child',parent='Marker')
    step('create_prefab',entity='Marker',path='Assets/Prefabs/Marker.kbprefab');step('delete_entity',entity='Marker')
    content='21kb-map2d 2\nsize 64 64\norientation flat\nbackground false\n'
    for y in range(32):
        for x in range(64):content+=f'prefab {x} {y} 0 "/Game/Prefabs/Marker.kbprefab"\n'
    step('write_file',path='Assets/Maps/Data.kbmap2d',content=content);step('discover_assets')
    step('create_entity',id='Map',name='World');step('add_component',entity='Map',component='World2DMap')
    step('assign_asset',entity='Map',role='tilemap',asset='/Game/Maps/Data.kbmap2d')
    inputs=[]
    for name,kind,value in [('level','int','2'),('enabled','bool','true'),('label','string','default'),('cost','float','1.5'),('lives','int','3')]:
        inputs+=['settings schema','field 0 '+name,'field 1 '+kind,'field 2 '+value,'apply']
    inputs+=['select 0 0 0 0','settings number-data','field 0 level','field 1 7','apply',
        'settings schema','field 0 rank','field 1 float','field 2 2.5','field 3 level']
    step('map2d_editor',entity='Map',checkpoint='schema',width=1100,height=900,inputs=inputs)
    step('map2d_editor',entity='Map',checkpoint='migrated',inputs=['apply','remember-map','key z ctrl','key y ctrl','assert-map-unchanged',
        'settings schema','field 0 rank','field 1 bool','field 2 true','reject','cancel','assert-map-unchanged',
        'settings position','field 0 8','field 1 40','field 2 0','apply','search Marker','click 25 185','click 710 450',
        'remember-map','key z ctrl','key y ctrl','assert-map-unchanged','key s ctrl'])
    step('assert_file',path='Assets/Maps/Data.kbmap2d',contains='propertySchema 6')
    source="""
local frame,previous=0,nil
function Tick(self,dt)
 frame=frame+1
 local map=World.FindByName('World')
 local count=CallFunction('Map2D.PrefabCount',{entity=map})
 if previous and frame>1 then assert(math.abs(count-previous)<=64,'Prefab budget exceeded') end
 previous=count
 local function region(active)
  assert(CallFunction('Map2D.SetPrefabRegion',{entity=map,minX=0,minY=0,maxX=active and 63 or -1,maxY=active and 63 or -1,budget=64}))
 end
 if frame==1 then region(true) end
 if frame==40 or frame==120 or frame==200 then
  assert(count==2049,'Missing or duplicate prefab instance: '..tostring(count))
  local rank=CallFunction('Map2D.GetNumber',{entity=map,x=0,y=0,name='rank'})
  assert(rank.found and rank.value==7,'Migrated override lost')
  assert(CallFunction('Map2D.GetBool',{entity=map,x=63,y=63,name='enabled'}).value,'Default flag lost')
  assert(CallFunction('Map2D.GetInteger',{entity=map,x=63,y=63,name='lives'}).value==3,'Default integer lost')
  assert(CallFunction('Map2D.GetText',{entity=map,x=63,y=63,name='label'}).value=='default','Default text lost')
  assert(CallFunction('Map2D.GetNumber',{entity=map,x=63,y=63,name='cost'}).value==1.5,'Default number lost')
  if frame==200 then assert(CallFunction('Map2D.Erase',{entity=map,x=0,y=0})) else region(false) end
 end
 if frame==80 or frame==160 then assert(count==0,'Streaming leaked instances');region(true) end
 if frame==219 then
  assert(count==2048,'Erasing a prefab did not release it')
  assert(CallFunction('Map2D.GetNumber',{entity=map,x=0,y=0,name='rank'}).value==2.5,'Erased cell did not inherit default')
  Log('tile-data-workflow-pass')
 end
end
"""
    step('write_file',path='Assets/Scripts/Observe.lua',content=source);step('discover_assets')
    step('create_entity',id='Observer',name='Observer');step('attach_script',entity='Observer',asset='/Game/Scripts/Observe.lua')
    step('save_scene');step('set_startup_scene',path='/Game/Scenes/TileData.21kbscene');step('open_scene',path='Assets/Scenes/TileData.21kbscene')
    step('play');step('step',frames=220,dt=.02);step('assert_console',contains='tile-data-workflow-pass')
    step('capture_runtime',checkpoint='instances',require_non_uniform=True);step('assert_no_errors');step('stop')
    scenario=root/'Scenario.json';scenario.write_text(json.dumps(dict(schema='21kb.editor-automation/v1',steps=steps),indent=2),encoding='utf-8')
    run(args.editor,'--selftest-scenario',scenario,'--selftest-root',root,'--selftest-task','Run')
    project=root/'Run/workspace'
    run(args.cli,'map2d','schema','--project',project,'--map','/Game/Maps/Data.kbmap2d','--name','extra','--type','int','--default','9')
    saved=(project/'Assets/Maps/Data.kbmap2d').read_text();assert 'propertySchema 7' in saved
    run(args.cli,'map2d','schema','--project',project,'--map','/Game/Maps/Data.kbmap2d','--name','rank','--type','bool','--default','true',expected=1)
    assert (project/'Assets/Maps/Data.kbmap2d').read_text()==saved,'Failed migration changed the map'
    replay=root/'tile-data.inputrecording';replay.write_bytes(recording([dict(keys=[]) for _ in range(220)]))
    assert 'tile-data-workflow-pass' in run(args.cli,'run','--project',project,'--frames','220','--input-replay',replay)
    package=root/'Package';package.mkdir()
    run(args.cooker,'--project',project,'--target','Windows.x64','--output',package/'Game.kbpack','--engine-root',args.engine_root.resolve(),
        '--runtime-modules-output',package/'RuntimeModules','--cache',args.test_root.resolve()/'CookCache')
    shutil.copy2(args.player,package/args.player.name)
    for dll in [*args.player.resolve().parent.glob('*.dll'),*args.plugin]:shutil.copy2(dll,package/dll.name)
    hidden=root/'SourceUnavailable';assert project.resolve().is_relative_to(root) and hidden.resolve().is_relative_to(root);project.rename(hidden)
    assert 'errors=0' in run(package/args.player.name,'--headless','--frames=8')
    result=run(args.player,'--project='+str(package/'Game.kbpack'),'--headless','--frames=220','--uncapped','--width=640','--height=480',
        '--input-replay='+str(replay),'--screenshot-file='+str(package/'instances.png'),'--screenshot-frame=218')
    assert 'tile-data-workflow-pass' in result and 'errors=0' in result,result
    (root/'result.json').write_text(json.dumps(dict(result='PASS',sourceUnavailable=True,prefabs=2049)),encoding='utf-8');print(root)

if __name__=='__main__':main()
