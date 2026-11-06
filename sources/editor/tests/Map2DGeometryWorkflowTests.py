"""Qualify map topology and persistent, deformable vector assets in the editor and player."""
import argparse
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('editor', 'cli', 'cooker', 'player', 'engine-root', 'shaderc', 'test-root'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--plugin', type=Path, action='append', default=[])
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='Map2DGeometry', dir=args.test_root)).resolve()
    project = root / 'SampleScene'
    history = []
    environment = os.environ.copy()
    environment['CMAKE_BUILD_PARALLEL_LEVEL'] = '2'

    def run(exe, *tail, expected=0):
        command = list(map(str, [exe.resolve(), *tail]))
        result = subprocess.run(command, cwd=exe.resolve().parent, env=environment,
            capture_output=True, encoding='utf-8', errors='replace', timeout=240)
        history.append(dict(command=command, exitCode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        (root / 'commands.json').write_text(json.dumps(history, indent=2), encoding='utf-8')
        assert result.returncode == expected, history[-1]
        return result.stdout + result.stderr

    def cli(*tail, **options):
        return run(args.cli, *tail, **options)

    def entity(name, position='0,0,0', preset=None, **options):
        tail = ['entity', 'add', '--project', project, '--name', name, '--position', position]
        if preset:
            tail += ['--create', preset]
        for key, value in options.items():
            tail += ['--' + key.replace('_', '-'), value]
        cli(*tail)

    def component(name, kind, *values, action='set'):
        cli('component', action, '--project', project, '--entity', name, '--type', kind,
            *[part for value in values for part in ('--set', value)])

    cli('new', '--parent', root, '--name', project.name, '--template', 'Empty')
    from PIL import Image, ImageDraw
    for name, topology, x, height in [('Iso','isometric',-3,16),('Hex','hexagonal',3,37)]:
        cli('map2d','new','--project',project,'--name',name,'--size','4x4','--orientation','wall','--topology',topology)
        component(name,'Transform',f'localPosition.x={x}')
        image=Image.new('RGBA',(32,((height+31)//32)*32),(0,0,0,0));draw=ImageDraw.Draw(image)
        outline=[(16,0),(31,height//2),(16,height-1),(0,height//2)] if name=='Iso' else [(16,0),(31,9),(31,27),(16,36),(0,27),(0,9)]
        draw.polygon(outline,fill=(230,55,25,255) if name=='Iso' else (25,200,70,255))
        image.save(project/f'Assets/{name}.png')
        map_path='/Game/Maps/'+name+'.kbmap2d'
        cli('map2d','fill','--project',project,'--map',map_path,'--sprite','/Game/'+name+'.png','--cell','0','--at','0,0')
        file=project/f'Assets/Maps/{name}.kbmap2d'
        text=file.read_text(encoding='utf-8').replace('sheet 32 1 2','sheet 32 1 1')
        lines=[line for line in text.splitlines() if not line.startswith('layout ')]
        at=next(i for i,line in enumerate(lines) if line.startswith('sheet '))
        lines.insert(at+1,f'layout 0 {height} 0 0 32 {((height+31)//32)*32}')
        file.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        assert topology in cli('map2d','info','--project',project,'--map',map_path)
        path=json.loads(cli('map2d','path','--project',project,'--map',map_path,'--from','1,1','--to','0,2'))
        (root/(name+'-path.json')).write_text(json.dumps(path,indent=2),encoding='utf-8')
        if name=='Hex':
            before=file.read_bytes()
            assert 'topology' in cli('map2d','set','--project',project,'--map',map_path,'--topology','invalid',expected=1)
            assert file.read_bytes()==before
    entity('Camera','0,2,-12','Camera',look_at='0,2,0')
    component('Camera','Camera','primary=true','projection=1','orthographicHeight=10','priority=200')
    entity('NativeHost','-3,-1.5,-.2');entity('LuaHost','3,-1.5,-.2')
    entity('Ambient');component('Ambient','Ambient Radiance',action='add');component('Ambient','Ambient Radiance','mode=0','color=1,1,1')
    for name,kind,points in [('Polygon','Polygon',[[-1,-.5],[1,-.5],[1,.5],[0,0],[-1,.5]]),('Line','Line',[[-1,0],[0,.75],[1,0]])]:
        cli('vector2d-new','--project',project,'--name',name,'--kind',kind,'--points',json.dumps(points))
        shape='/Game/Shapes/'+name+'.kbvector2d'
        if kind=='Line': cli('vector2d-set','--project',project,'--shape',shape,'--width','.2')
        shown=json.loads(cli('vector2d-show','--project',project,'--shape',shape));assert shown['points']==points
    invalid=project/'Assets/Shapes/Polygon.kbvector2d';before=invalid.read_bytes()
    assert 'intersect' in cli('vector2d-set','--project',project,'--shape','/Game/Shapes/Polygon.kbvector2d',
        '--points','[[0,0],[1,1],[0,1],[1,0]]',expected=1)
    assert invalid.read_bytes()==before
    cli('script-new','--project',project,'--name','NativeMapGeometry','--language','cpp')
    source=project/'Source/NativeScripts/NativeMapGeometry/NativeMapGeometry.cpp'
    source.write_text(r'''#include "engine/script/NativeScriptPlugin.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneVectorShapes2D.hpp"
#include "engine/scene/SceneSurfaceGeometry.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTilemapRuntime.hpp"
#include "engine/assets/Map2DNavigation.hpp"
#include <map>
#include <array>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <cmath>
extern "C" void ecs_os_set_api_defaults(void);
static std::map<std::pair<std::uint64_t,std::uint64_t>,int> frames;
static void Require(bool ok,const std::string& error){if(!ok) throw std::runtime_error(error);}
static void Ready(kb::script::ScriptExecutionContext* c){frames[{c->GetScene().Id(),c->Self().Id()}]=0;}
static void Tick(kb::script::ScriptExecutionContext* c){
 auto& s=c->GetScene();auto& f=frames[{s.Id(),c->Self().Id()}];++f;std::string error;
 if(f==10){
  Require(kb::scene::SceneVectorShapes2D::Load(s,c->Self(),"/Game/Shapes/Polygon.kbvector2d",error),error);
  auto shape=kb::scene::SceneVectorShapes2D::Get(s,c->Self());Require(shape&&shape->points.size()==5,"saved concave polygon missing");
  auto bad=*shape;bad.points={{0,0},{1,1},{0,1},{1,0}};
  Require(!kb::scene::SceneVectorShapes2D::Set(s,c->Self(),bad,error),"crossed polygon accepted");
  Require(kb::scene::SceneVectorShapes2D::Get(s,c->Self())->points==shape->points,"invalid edit changed surface");
  Require(!kb::scene::SceneVectorShapes2D::SetWidth(s,c->Self(),.5F,error),"polygon accepted line width");
  for(const auto* name:{"Iso","Hex"}){
   const auto found=c->CallFunction("World.FindByName",std::array{kb::script::ScriptFunctionArgument{"name",kb::script::ScriptValue{std::string{name}}}});
   Require(found.Succeeded(),"map entity lookup failed");const kb::scene::SceneEntity owner{found.Output("entity")->AsUInt64()};
   auto* runtime=s.Runtime().Tilemaps();const auto* map=runtime?runtime->Map(owner):nullptr;Require(map!=nullptr,"tilemap not initialized");
   const bool hex=map->topology==kb::assets::Map2DTopology::Hexagonal;
   Require(hex||map->topology==kb::assets::Map2DTopology::Isometric,"map topology lost");
   Require(kb::assets::Map2DNeighbourOffsets(map->topology).size()==(hex?6U:4U),"wrong neighbors");
   const auto path=kb::assets::FindMap2DPathResult(*map,{1,1},{0,2});Require(path.status==kb::assets::Map2DPathStatus::Found&&path.cost==(hex?10:20),"wrong topology path");
   for(int y=0;y<4;++y) for(int x=0;x<4;++x){const auto p=kb::assets::Map2DProjectGrid(*map,x+.5,y+.5);const auto cell=kb::assets::Map2DCellAtPlane(*map,p[0],p[1]);Require(cell&&(*cell)[0]==x&&(*cell)[1]==y,"projection roundtrip failed");}
   auto tile=map->tiles.at(kb::assets::Map2DTileKey(0,0,0));tile.ground.tint=0xff00ffff;
   Require(runtime->SetTile(owner,0,0,0,tile,&error),error);Require(runtime->Map(owner)->tiles.at(kb::assets::Map2DTileKey(0,0,0)).ground.tint==0xff00ffff,"live tile edit lost");
  }
 }
 if(f==50){Require(kb::scene::SceneVectorShapes2D::SetPoint(s,c->Self(),2,{1,1.6F},error),error);}
 if(f==70){const auto shape=kb::scene::SceneVectorShapes2D::Get(s,c->Self());Require(shape&&shape->points[2][1]>1.5F,"polygon did not deform");
  const auto file=std::filesystem::temp_directory_path()/("21kb-vector-"+std::to_string(s.Id())+"-"+std::to_string(c->Self().Id())+".kbvector2d");
  Require(kb::scene::SaveVectorShape2D(file,*shape,error),error);
  std::ifstream input(file,std::ios::binary);std::string saved{std::istreambuf_iterator<char>{input},{}};input.close();std::filesystem::remove(file);
  kb::scene::VectorShape2DAsset reloaded;Require(kb::scene::DecodeVectorShape2D(saved,reloaded,error)&&reloaded.points==shape->points,"deformed polygon save lost points");
  bool drawn=false;for(const auto& surface:kb::scene::SceneSurfaceGeometry::All(s)) if(surface.owner==c->Self()) drawn=surface.indices.size()==9;
  for(const auto& surface:kb::scene::SceneSurfaceGeometry::All(s)) for(std::size_t i=0;i<surface.indices.size();i+=3){
   const auto a=surface.vertices[surface.indices[i]].position,b=surface.vertices[surface.indices[i+1]].position,d=surface.vertices[surface.indices[i+2]].position;
   Require((b.x-a.x)*(d.y-a.y)-(b.y-a.y)*(d.x-a.x)<0,"vector triangle disagrees with front normal");
  }
  Require(drawn,"concave triangulation missing");Require(c->CallFunction("Log",std::array{kb::script::ScriptFunctionArgument{"message",kb::script::ScriptValue{std::string{"native-map-geometry PASS"}}}}).Succeeded(),"log failed");}
}
KB_NATIVE_SCRIPT_PLUGIN_EXPORT bool kb_register_native_scripts(kb::script::NativeScriptPluginApi* api){ecs_os_set_api_defaults();return api&&api->version==kb::script::kNativeScriptPluginApiVersion&&api->registerLifecycle(api->user,"project.NativeMapGeometry",kb::script::ScriptLifecycleEvent::Ready,&Ready)&&api->registerLifecycle(api->user,"project.NativeMapGeometry",kb::script::ScriptLifecycleEvent::Tick,&Tick);}
''',encoding='utf-8')
    descriptor=project/'Assets/Scripts/NativeMapGeometry.native'
    descriptor.write_text('# @asset /Game/Shapes/Polygon.kbvector2d\n'+descriptor.read_text(encoding='utf-8'),encoding='utf-8')
    cli('script-build','--project',project,'--script','/Game/Scripts/NativeMapGeometry.native')
    cli('scene-attach','--project',project,'--scene','/Game/Scenes/Main.21kbscene','--node','NativeHost','--script','/Game/Scripts/NativeMapGeometry.native')
    cli('script-new','--project',project,'--name','LuaMapGeometry','--language','lua')
    (project/'Assets/Scripts/LuaMapGeometry.lua').write_text('''-- @asset /Game/Shapes/Line.kbvector2d
local frame=0
function Ready(self) frame=0 end
function Tick(self,dt)
 frame=frame+1
 if frame==10 then
  assert(Vector2D.Load(self.entity,"/Game/Shapes/Line.kbvector2d"))
  for _,name in ipairs({"Iso","Hex"}) do
   local owner=World.FindByName(name)
   local p=CallFunction("Map2D.CellToWorld",{entity=owner,x=1,y=1})
   local cell=CallFunction("Map2D.WorldToCell",{entity=owner,x=p.x,y=p.y,z=p.z})
   assert(cell.found and cell.x==1 and cell.y==1)
   local neighbor=CallFunction("Map2D.Neighbor",{entity=owner,x=1,y=1,index=0})
   assert(neighbor.found and neighbor.count==(name=="Hex" and 6 or 4))
  end
 end
 if frame==50 then assert(Vector2D.SetPoint(self.entity,1,0,1.7));assert(Vector2D.SetWidth(self.entity,.45)) end
 if frame==70 then
  local p=CallFunction("Vector2D.Point",{entity=self.entity,index=1});assert(p.count==3 and p.y>1.6)
  Log("lua-map-geometry PASS")
 end
end
''',encoding='utf-8')
    cli('scene-attach','--project',project,'--scene','/Game/Scenes/Main.21kbscene','--node','LuaHost','--script','/Game/Scripts/LuaMapGeometry.lua')
    initial = root / 'EditorSource'
    shutil.copytree(project, initial, ignore=shutil.ignore_patterns('Saved'))
    scenario = root / 'scenario.json'
    scenario.write_text(json.dumps(dict(schema='21kb.editor-automation/v1', initial_project=str(next(initial.glob('*.21kbproject'))), steps=[
        dict(op='configure_runtime_rendering', width=640, height=360), dict(op='open_scene', path='Assets/Scenes/Main.21kbscene'),
        dict(op='save_scene'), dict(op='open_scene', path='Assets/Scenes/Main.21kbscene'), dict(op='play'),
        dict(op='step', frames=20), dict(op='capture_runtime', checkpoint='early', require_non_uniform=True),
        dict(op='step', frames=60), dict(op='capture_runtime', checkpoint='late', require_non_uniform=True),
        dict(op='assert_console', contains='native-map-geometry PASS'), dict(op='assert_console', contains='lua-map-geometry PASS'),
        dict(op='assert_no_errors'), dict(op='stop')])), encoding='utf-8')
    run(args.editor, '--selftest-scenario', scenario, '--selftest-root', root / 'Editor', '--selftest-task', 'Geometry')
    sys.path.insert(0, str(args.engine_root.resolve() / 'scripts'))
    from png_readback import read_png
    early, late = [read_png(root / f'Editor/Geometry/automation/screenshots/{name}.png') for name in ('early', 'late')]
    changes = []
    for left,right in ((150,300),(340,490)):
        changed=sum(max(abs(a-b) for a,b in zip(early[y][x][:3],late[y][x][:3]))>8 for y in range(190,330) for x in range(left,right))
        assert changed>30, ('vector deformation invisible on GPU',left,changed)
        changes.append(changed)
    projected_samples=[]
    for name,actor_x,channel in [('Iso',-3,0),('Hex',3,1)]:
        for q in range(4):
            for r in range(4):
                if q==0 and r==0: continue
                if name=='Iso': x=(q-r)*.5; y=2-(q+r+1)*.25
                else: x=q+.5+r*.5-2.75; y=(3*math.sqrt(3)*.5+2/math.sqrt(3))-(r*math.sqrt(3)*.5+1/math.sqrt(3))
                px=round(320+(actor_x+x)*36);py=round(180-(y-2)*36)
                rgb=early[py][px][:3]
                assert rgb[channel]>80 and rgb[channel]>rgb[2]*1.5, ('map cell rendered at wrong projection',name,q,r,px,py,rgb)
                projected_samples.append([name,q,r,px,py])
    output=run(args.player,'--project='+str(next(project.glob('*.21kbproject'))),'--headless','--frames=100','--uncapped','--width=640','--height=360',
        '--profile-fixed-step','--profile-file='+str(root/'source.csv'))
    assert 'native-map-geometry PASS' in output and 'lua-map-geometry PASS' in output and 'errors=0' in output,output
    package=root/'Package';package.mkdir()
    run(args.cooker,'--project',project,'--target','Windows.x64','--output',package/'Game.kbpack','--engine-root',args.engine_root,
        '--shaderc',args.shaderc,'--cache',root/'CookCache','--runtime-modules-output',package/'RuntimeModules')
    for plugin in args.plugin:shutil.copy2(plugin,package/plugin.name)
    for source,destination in [(project,root/'SourceUnavailable'),(initial,root/'EditorSourceUnavailable')]:
        assert source.resolve().parent==root and destination.resolve().parent==root
        source.rename(destination)
    output=run(args.player,'--project='+str(package/'Game.kbpack'),'--headless','--frames=100','--uncapped','--width=640','--height=360',
        '--profile-fixed-step','--profile-file='+str(root/'package.csv'))
    assert 'native-map-geometry PASS' in output and 'lua-map-geometry PASS' in output and 'errors=0' in output and 'shutdown=clean' in output,output
    (root/'result.json').write_text(json.dumps(dict(result='PASS',topologies=['isometric','hexagonal'],
        vectorGpuChangedPixels=changes,projectedCellSamples=projected_samples,nativeAndLua=True,invalidPolygonPreservesAsset=True,sourceUnavailable=True),indent=2),encoding='utf-8')
    print(root)


if __name__ == '__main__':
    main()
