"""Qualify weighted planar skeletal animation in the editor and player."""
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
    root = Path(tempfile.mkdtemp(prefix='Rig2D', dir=args.test_root)).resolve()
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

    cli('new','--parent',root,'--name',project.name,'--template','Empty')
    cli('rig2d','new','--project',project,'--name','Ribbon')
    rig_path='/Game/Rigs/Ribbon.kbrig2d'
    data=json.loads(cli('rig2d','show','--project',project,'--rig',rig_path));data['duration']=3;data['tracks'][0]['keys'][-1]['time']=3
    cli('rig2d','set','--project',project,'--rig',rig_path,'--data',json.dumps(data))
    cli('rig2d','build','--project',project,'--rig',rig_path)
    source_rig=project/'Assets/Rigs/Ribbon.kbrig2d';before=source_rig.read_bytes()
    invalid=json.loads(json.dumps(data));invalid['bones'][0]['parent']=1
    assert 'parent' in cli('rig2d','set','--project',project,'--rig',rig_path,'--data',json.dumps(invalid),expected=1)
    assert source_rig.read_bytes()==before
    invalid=json.loads(json.dumps(data));invalid['vertices'][0]['weights']=[.5,0,0,0]
    assert 'weights' in cli('rig2d','set','--project',project,'--rig',rig_path,'--data',json.dumps(invalid),expected=1)
    assert source_rig.read_bytes()==before
    entity('Camera','0,1,-10','Camera',look_at='0,1,0')
    component('Camera','Camera','primary=true','projection=1','orthographicHeight=6')
    entity('NativeHost','-2,0,0');entity('LuaHost','2,0,0')
    entity('Ambient');component('Ambient','Ambient Radiance',action='add');component('Ambient','Ambient Radiance','mode=0','color=1,1,1')
    cli('post-process-new','--project',project,'--name','Look','--set','autoExposureMetering=Manual','--set','temporalAntiAliasingEnabled=0','--set','bloomEnabled=0')
    cli('scene-post-process','--project',project,'--profile','/Game/PostProcess/Look.kbppfx')
    cli('script-new','--project',project,'--name','NativeRigProbe','--language','cpp')
    (project/'Source/NativeScripts/NativeRigProbe/NativeRigProbe.cpp').write_text(r'''#include "engine/script/NativeScriptPlugin.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAnimators.hpp"
#include "engine/scene/Rig2DAsset.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/assets/AssetManager.hpp"
#include <array>
#include <map>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>
extern "C" void ecs_os_set_api_defaults(void);
static std::map<std::pair<std::uint64_t,std::uint64_t>,int> frames;
static void Require(bool ok,const std::string& error){if(!ok) throw std::runtime_error(error);}
static void Ready(kb::script::ScriptExecutionContext* c){frames[{c->GetScene().Id(),c->Self().Id()}]=0;}
static void Tick(kb::script::ScriptExecutionContext* c){auto& s=c->GetScene();auto& f=frames[{s.Id(),c->Self().Id()}];++f;std::string error;
 if(f==5){
  kb::scene::Rig2DAsset source;source.bones={{"Root",-1,{}},{"Tip",0,{.position={0,1}}}};
  for(auto point:std::array<std::array<float,2>,3>{{{0,0},{1,0},{0,1}}}){kb::scene::Rig2DVertex v;v.position=point;source.vertices.push_back(v);}source.indices={0,1,2};
  kb::scene::CompiledRig2D built;Require(kb::scene::CompileRig2D(source,99,0,"/Game/Motion.kbanim",built,error),error);Require(built.skeleton.bones.size()==2&&built.mesh.lods[0].vertices.size()==3,"typed rig authoring failed");
  const auto file=std::filesystem::temp_directory_path()/("21kb-rig2d-"+std::to_string(s.Id())+"-"+std::to_string(c->Self().Id())+".kbrig2d");
  Require(kb::scene::SaveRig2D(file,source,error),error);std::ifstream input(file,std::ios::binary);std::string text{std::istreambuf_iterator<char>{input},{}};input.close();std::filesystem::remove(file);
  kb::scene::Rig2DAsset reloaded;Require(kb::scene::DecodeRig2D(text,reloaded,error)&&reloaded.bones.size()==2,"rig source save lost hierarchy");
  source.vertices[0].weights[0]=.5F;Require(!kb::scene::CompileRig2D(source,100,0,"/Game/Bad.kbanim",built,error)&&built.mesh.skeletonAssetId==99,"invalid weights changed compiled result");
  Require(kb::scene::AttachRig2D(s,c->Self(),"/Game/Rigs/Ribbon/Mesh.kbskeletalmesh","/Game/Rigs/Ribbon/Controller.kbanimcontroller",error),error);
 }
 if(f==10) Require(s.Animators().Play(c->Self(),"Base","Play"),"native clip did not play");
 if(f==20){const auto pose=s.Animators().InstanceSkeleton(c->Self());Require(pose&&pose->currentLocalPose.rotations.size()==2&&pose->currentLocalPose.rotations[1].z>.001F,"clip time did not advance");Require(s.Animators().SetSpeed(c->Self(),0),"native pause failed");}
 if(f==50) Require(s.Animators().SeekNormalized(c->Self(),1),"native skeletal seek failed");
 if(f==70){
  const auto pose=s.Animators().InstanceSkeleton(c->Self());Require(pose&&pose->boneIds.size()==2,"runtime rig pose missing");
  const auto* info=s.Assets().Manager().Registry().FindByPath("/Game/Rigs/Ribbon/Mesh.kbskeletalmesh");Require(info!=nullptr,"compiled rig mesh missing");
  const auto mesh=s.Assets().Manager().Load<kb::scene::SkeletalMeshAsset>(info->id);Require(mesh.IsLoaded(),"rig mesh not loaded");std::vector<kb::math::Mat4> palette;
  Require(kb::scene::BuildSkeletalMeshSkinningPalette(*mesh.Shared(),pose->boneIds,pose->currentSkinMatrices,palette),"rig skin palette failed");
  const auto& vertex=mesh.Shared()->lods[0].vertices[2];kb::math::Vec4 result{};
  for(unsigned i=0;i<4;++i){auto point=palette[vertex.jointIndices[i]]*kb::math::Vec4{vertex.position.x,vertex.position.y,vertex.position.z,1};result.x+=point.x*vertex.jointWeights[i];result.y+=point.y*vertex.jointWeights[i];}
  const double angle=70*3.141592653589793/180;const double expectedX=-.2+.5*(.5-.4*std::cos(angle)),expectedY=1-.2*std::sin(angle);
  Require(std::abs(result.x-expectedX)<.0001&&std::abs(result.y-expectedY)<.0001,"weighted two-bone deformation is incorrect");
  Require(c->CallFunction("Log",std::array{kb::script::ScriptFunctionArgument{"message",kb::script::ScriptValue{std::string{"native-rig2d PASS"}}}}).Succeeded(),"native log failed");
 }
}
KB_NATIVE_SCRIPT_PLUGIN_EXPORT bool kb_register_native_scripts(kb::script::NativeScriptPluginApi* api){ecs_os_set_api_defaults();return api&&api->version==kb::script::kNativeScriptPluginApiVersion&&api->registerLifecycle(api->user,"project.NativeRigProbe",kb::script::ScriptLifecycleEvent::Ready,&Ready)&&api->registerLifecycle(api->user,"project.NativeRigProbe",kb::script::ScriptLifecycleEvent::Tick,&Tick);}
''',encoding='utf-8')
    descriptor=project/'Assets/Scripts/NativeRigProbe.native'
    annotations='# @asset /Game/Rigs/Ribbon/Mesh.kbskeletalmesh\n# @asset /Game/Rigs/Ribbon/Controller.kbanimcontroller\n'
    descriptor.write_text(annotations+descriptor.read_text(encoding='utf-8'),encoding='utf-8')
    cli('script-build','--project',project,'--script','/Game/Scripts/NativeRigProbe.native')
    cli('scene-attach','--project',project,'--scene','/Game/Scenes/Main.21kbscene','--node','NativeHost','--script','/Game/Scripts/NativeRigProbe.native')
    cli('script-new','--project',project,'--name','LuaRigProbe','--language','lua')
    (project/'Assets/Scripts/LuaRigProbe.lua').write_text('''-- @asset /Game/Rigs/Ribbon/Mesh.kbskeletalmesh
-- @asset /Game/Rigs/Ribbon/Controller.kbanimcontroller
local frame=0
function Ready(self) frame=0 end
function Tick(self,dt)
 frame=frame+1
 if frame==5 then assert(Rig2D.Attach(self.entity,"/Game/Rigs/Ribbon/Mesh.kbskeletalmesh","/Game/Rigs/Ribbon/Controller.kbanimcontroller")) end
 if frame==10 then assert(CallFunction("Animator.Play",{entity=self.entity,layer="Base",state="Play"})) end
 if frame==20 then assert(CallFunction("Animator.SetSpeed",{entity=self.entity,speed=0})) end
 if frame==50 then assert(CallFunction("Animator.Play",{entity=self.entity,layer="Base",state="Play",normalizedTime=1})) end
 if frame==70 then
  local state=CallFunction("Animator.State",{entity=self.entity,layer="Base"})
  assert(state.state=="Play" and state.normalizedTime>.99)
  Log("lua-rig2d PASS")
 end
end
''',encoding='utf-8')
    cli('scene-attach','--project',project,'--scene','/Game/Scenes/Main.21kbscene','--node','LuaHost','--script','/Game/Scripts/LuaRigProbe.lua')
    initial = root / 'EditorSource'
    shutil.copytree(project, initial, ignore=shutil.ignore_patterns('Saved'))
    scenario = root / 'scenario.json'
    scenario.write_text(json.dumps(dict(schema='21kb.editor-automation/v1', initial_project=str(next(initial.glob('*.21kbproject'))), steps=[
        dict(op='configure_runtime_rendering', width=640, height=360), dict(op='open_scene', path='Assets/Scenes/Main.21kbscene'),
        dict(op='save_scene'), dict(op='open_scene', path='Assets/Scenes/Main.21kbscene'), dict(op='play'),
        dict(op='step', frames=20), dict(op='capture_runtime', checkpoint='early', require_non_uniform=True),
        dict(op='step', frames=60), dict(op='capture_runtime', checkpoint='late', require_non_uniform=True),
        dict(op='assert_console', contains='native-rig2d PASS'), dict(op='assert_console', contains='lua-rig2d PASS'),
        dict(op='assert_no_errors'), dict(op='stop')])), encoding='utf-8')
    run(args.editor, '--selftest-scenario', scenario, '--selftest-root', root / 'Editor', '--selftest-task', 'Geometry')
    sys.path.insert(0, str(args.engine_root.resolve() / 'scripts'))
    from png_readback import read_png
    early, late = [read_png(root / f'Editor/Geometry/automation/screenshots/{name}.png') for name in ('early', 'late')]
    changes=[]
    for left,right in ((100,280),(350,540)):
        changed=sum(max(abs(a-b) for a,b in zip(early[y][x][:3],late[y][x][:3]))>8 for y in range(60,300) for x in range(left,right))
        assert changed>200, ('weighted planar rig did not deform on GPU',left,changed)
        changes.append(changed)
    output=run(args.player,'--project='+str(next(project.glob('*.21kbproject'))),'--headless','--frames=100','--uncapped','--width=640','--height=360',
        '--profile-fixed-step','--profile-file='+str(root/'source.csv'))
    assert 'native-rig2d PASS' in output and 'lua-rig2d PASS' in output and 'errors=0' in output,output
    package=root/'Package';package.mkdir()
    run(args.cooker,'--project',project,'--target','Windows.x64','--output',package/'Game.kbpack','--engine-root',args.engine_root,
        '--shaderc',args.shaderc,'--cache',root/'CookCache','--runtime-modules-output',package/'RuntimeModules')
    for plugin in args.plugin:shutil.copy2(plugin,package/plugin.name)
    for source,destination in [(project,root/'SourceUnavailable'),(initial,root/'EditorSourceUnavailable')]:
        assert source.resolve().parent==root and destination.resolve().parent==root
        source.rename(destination)
    output=run(args.player,'--project='+str(package/'Game.kbpack'),'--headless','--frames=100','--uncapped','--width=640','--height=360',
        '--profile-fixed-step','--profile-file='+str(root/'package.csv'))
    assert 'native-rig2d PASS' in output and 'lua-rig2d PASS' in output and 'errors=0' in output and 'shutdown=clean' in output,output
    (root/'result.json').write_text(json.dumps(dict(result='PASS',gpuChangedPixels=changes,
        nativeAndLua=True,weightedVertexVerified=True,invalidHierarchyAndWeightsPreserveSource=True,sourceUnavailable=True),indent=2),encoding='utf-8')
    print(root)


if __name__ == '__main__':
    main()
