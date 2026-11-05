"""Observe authored ragdoll, cloth, vehicle, fracture and buoyancy in one saved scene."""
import argparse
import json
import os
from pathlib import Path
import re
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
    root = Path(tempfile.mkdtemp(prefix='PhysicsAssembly', dir=args.test_root)).resolve()
    project = root / 'SampleScene'
    history = []
    environment = os.environ.copy()
    environment['CMAKE_BUILD_PARALLEL_LEVEL'] = '2'

    def run(exe, *tail, expected=0):
        command = list(map(str, [exe.resolve(), *tail]))
        result = subprocess.run(command, cwd=Path(command[0]).parent, env=environment,
            capture_output=True, encoding='utf-8', errors='replace', timeout=240)
        history.append(dict(command=command, exitCode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        (root / 'commands.json').write_text(json.dumps(history, indent=2), encoding='utf-8')
        assert result.returncode == expected, history[-1]
        return result.stdout + result.stderr

    def cli(*tail, **options):
        return run(args.cli, *tail, **options)

    def entity(name, position='0,0,0', preset=None, scale='1,1,1', **options):
        tail = ['entity', 'add', '--project', project, '--name', name, '--position', position, '--scale', scale]
        if preset:
            tail += ['--create', preset]
        for key, value in options.items():
            tail += ['--' + key.replace('_', '-'), value]
        cli(*tail)

    def component(name, kind, *values, action='set'):
        cli('component', action, '--project', project, '--entity', name, '--type', kind,
            *[part for value in values for part in ('--set', value)])

    cli('new', '--parent', root, '--name', project.name, '--template', 'Empty')
    entity('MainCamera', '0,4,-12', 'Camera', look_at='0,1.5,0')
    component('MainCamera', 'Camera', 'verticalFovDegrees=50')
    entity('Floor', '0,-0.5,0', 'Cube', '40,1,40', body='Static')
    entity('Rig', '-1.5,3,0')
    entity('Car', '3,1,0')
    entity('Breakable', '0.5,2,2')
    entity('Float', '-6,0.5,2', 'Cube', body='Dynamic')
    component('Float', 'Rigidbody', 'mass=0.5', 'linearDamping=0.2')
    entity('Ambient')
    component('Ambient', 'Ambient Radiance', action='add')
    component('Ambient', 'Ambient Radiance', 'mode=0', 'color=0.18,0.18,0.18')
    entity('Sun', '0,5,-3', 'Directional Light', rotation='45,0,0')
    cli('post-process-new', '--project', project, '--name', 'Look', '--set', 'autoExposureMetering=Manual',
        '--set', 'temporalAntiAliasingEnabled=0', '--set', 'bloomEnabled=0')
    cli('scene-post-process', '--project', project, '--profile', '/Game/PostProcess/Look.kbppfx')
    folder = project / 'Assets/Physics'
    folder.mkdir(parents=True)
    profiles = {
        'Rig': dict(kind='Ragdoll', bones=[dict(name='Root', parent=-1, center=[0, .4, 0], radius=.14, height=.8, mass=5),
                                        dict(name='Tip', parent=0, center=[0, .4, 0], radius=.14, height=.8, mass=5)]),
        'Cloth': dict(kind='Cloth', columns=8, rows=8, spacing=.25, mass=2, compliance=.00001, bendingCompliance=.001, pinTopEdge=True),
        'Car': dict(kind='Vehicle', size=[1.6, .5, 3], mass=700, wheelRadius=.35, suspensionLength=.4, engineTorque=300),
        'Breakable': dict(kind='Fracture', size=[1.5, 1.5, 1.5], mass=10, cellsX=2, cellsY=2, cellsZ=2),
        'Float': dict(kind='Buoyancy', waterLevel=1, buoyancy=1.5, linearDrag=2, angularDrag=.5, fluidVelocity=[0, 0, 0]),
    }
    for name, value in profiles.items():
        (folder / (name + '.kbphysics')).write_text(json.dumps(dict(schema='21kb.physics-assembly/v1', **value)), encoding='utf-8')
    skeletal = project / 'Assets/Skeletal'
    skeletal.mkdir()
    (skeletal / 'Rig.kbskeleton').write_text('21kb Skeleton 1\n'
        'bone 1 -1 "Root" 0 0 0 0 0 0 1 1 1 1\ninverseBind 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1\n'
        'bone 2 0 "Tip" 0 0.8 0 0 0 0 1 1 1 1\ninverseBind 1 0 0 0 0 1 0 0 0 0 1 0 0 -0.8 0 1\n', encoding='utf-8')
    shown = cli('skeleton', 'show', '--project', project, '--skeleton', '/Game/Skeletal/Rig.kbskeleton')
    binding = re.search(r'assetId (\d+) compatibilitySignature (\d+)', shown)
    assert binding, shown
    skeleton_id, signature = binding.groups()
    mesh = [f'21kb SkeletalMesh 1\nskeleton {skeleton_id} {signature}\nbounds 0 0.8 0 3 3 1\nlod 1\n']
    for bone in range(2):
        for x, y, u, v in [(-.18, bone * .8, 0, 0), (.18, bone * .8, 1, 0),
                            (-.18, (bone + 1) * .8, 0, 1), (.18, (bone + 1) * .8, 1, 1)]:
            values = [0, x, y, 0, 0, 0, -1, 1, 0, 0, 1, u, v, bone, 0, 0, 0, 1, 0, 0, 0]
            mesh.append('vertex ' + ' '.join(map(str, values)) + '\n')
    for index in (0, 2, 1, 1, 2, 3, 4, 6, 5, 5, 6, 7):
        mesh.append(f'index 0 {index}\n')
    mesh.append('section 0 0 12 0\nsectionBone 0 0 1\nsectionBone 0 0 2\nrequiredBone 0 1\nrequiredBone 0 2\n')
    (skeletal / 'Rig.kbskeletalmesh').write_text(''.join(mesh), encoding='utf-8')
    motion = project / 'Assets/Animation'
    motion.mkdir()
    (motion / 'Idle.kbanim').write_text(f'21kb AnimationClip 2\ndurationSeconds 2\nlooping 1\ntargetSkeleton {skeleton_id} {signature}\n'
        'skeletalTrack 2 18446744073709551615\nskeletalKey 0 0 0 0.8 0 0 0 0 1 1 1 1\n'
        'skeletalKey 0 1 0.1 0.8 0 0 0 0.0998334 0.9950042 1 1 1\n'
        'skeletalKey 0 2 0 0.8 0 0 0 0 1 1 1 1\nrootMotion None 0\n', encoding='utf-8')
    cli('animator', 'new', '--project', project, '--name', 'Rig', '--clip', '/Game/Animation/Idle.kbanim')
    component('Rig', 'MeshRenderer', action='add')
    component('Rig', 'MeshRenderer', 'mesh=/Game/Skeletal/Rig.kbskeletalmesh')
    component('Rig', 'Animator', action='add')
    component('Rig', 'Animator', 'controller=/Game/Animation/Rig.kbanimcontroller')
    cli('script-new', '--project', project, '--name', 'NativePhysicsProbe', '--language', 'cpp')
    native = project / 'Source/NativeScripts/NativePhysicsProbe/NativePhysicsProbe.cpp'
    native.write_text(r'''#include "engine/script/NativeScriptPlugin.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/scene/PhysicsAssembly.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneAnimators.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneRigidbodyComponents.hpp"
#include <array>
#include <map>
#include <stdexcept>
extern "C" void ecs_os_set_api_defaults(void);
struct Probe {int frame=0;std::uint64_t rag=0,cloth=0;kb::scene::SceneEntity surface{};bool bound=false,returned=false;};
static std::map<std::pair<std::uint64_t,std::uint64_t>,Probe> probes;
static void Require(bool condition,const std::string& message) {if(!condition) throw std::runtime_error(message);}
static void Ready(kb::script::ScriptExecutionContext* c) {probes[{c->GetScene().Id(),c->Self().Id()}]=Probe{};}
static void Tick(kb::script::ScriptExecutionContext* c) {
    auto& scene=c->GetScene();auto& p=probes[{scene.Id(),c->Self().Id()}];++p.frame;std::string error;
    if(p.frame==15) {
        kb::scene::PhysicsAssemblyAsset bad;bad.columns=1;
        Require(!kb::scene::PhysicsAssemblies::Create(scene,c->Self(),bad).Succeeded(),"invalid assembly was accepted");
        kb::scene::PhysicsAssemblyAsset decoded;decoded.columns=9;
        Require(!kb::scene::DecodePhysicsAssembly("{\"schema\":\"21kb.physics-assembly/v1\",\"kind\":\"Cloth\",\"columns\":1}",decoded,error)&&decoded.columns==9,"invalid asset decode mutated its destination");
        const auto rag=kb::scene::PhysicsAssemblies::Load(scene,c->Self(),"/Game/Physics/Rig.kbphysics");Require(rag.Succeeded(),rag.error);p.rag=rag.handle;
        kb::scene::SceneObjectDesc surface;surface.name="Cloth";surface.transform.localPosition={-3,1.4F,0};surface.transform.localRotation={0.70710678F,0,0,0.70710678F};
        p.surface=scene.Entities().CreateEntity(surface);
        const auto cloth=kb::scene::PhysicsAssemblies::Load(scene,p.surface,"/Game/Physics/Cloth.kbphysics");Require(cloth.Succeeded(),cloth.error);p.cloth=cloth.handle;
        Require(!kb::scene::PhysicsAssemblies::Drive(scene,p.cloth,1,0,0,error)&&!error.empty(),"wrong assembly operation succeeded");
    }
    if(p.frame==45) {
        kb::scene::PhysicsAssemblyState state;Require(kb::scene::PhysicsAssemblies::State(scene,p.rag,state,error)&&state.ready&&state.entities.size()==2,"ragdoll did not create live bone bodies");
        Require(kb::scene::PhysicsBackend::AddImpulse(scene,state.entities[1],{3,0,0}),"ragdoll body impulse failed");
    }
    if(p.frame==85) {
        kb::scene::PhysicsAssemblyState state;Require(kb::scene::PhysicsAssemblies::State(scene,p.rag,state,error),error);
        const auto pose=scene.Animators().InstanceSkeleton(c->Self());Require(pose.has_value(),"ragdoll skeleton disappeared");
        const auto owner=scene.Transforms().Get(c->Self()),body=scene.Transforms().Get(state.entities[1]);
        const auto skinWorld=owner.worldPosition+kb::math::Rotate(owner.worldRotation,pose->currentComponentPose.positions[1]);
        const auto physicsWorld=body.worldPosition-kb::math::Rotate(body.worldRotation,kb::scene::Vec3{0,.4F,0});
        Require(kb::math::Length(skinWorld-physicsWorld)<.3F&&owner.worldPosition.y<2.8F,"simulated bones did not reach the rendered skeleton");p.bound=true;
    }
    if(p.frame==90) Require(kb::scene::PhysicsAssemblies::SimulateRagdoll(scene,p.rag,false,error),error);
    if(p.frame==110) {
        kb::scene::PhysicsAssemblyState state;Require(kb::scene::PhysicsAssemblies::State(scene,p.rag,state,error)&&state.ready&&!state.simulated,"ragdoll did not return to animation");
        const auto pose=scene.Animators().InstanceSkeleton(c->Self());Require(pose.has_value(),"animation handback lost the skeleton");
        const auto owner=scene.Transforms().Get(c->Self()),body=scene.Transforms().Get(state.entities[1]);
        const auto center=owner.worldPosition+kb::math::Rotate(owner.worldRotation,pose->currentComponentPose.positions[1])+kb::math::Rotate(body.worldRotation,kb::scene::Vec3{0,.4F,0});
        Require(scene.Components().Rigidbodies().TryGet(state.entities[1])->bodyType==kb::scene::RigidbodyBodyType::Kinematic&&kb::math::Length(center-body.worldPosition)<.2F,"animated pose did not drive kinematic bone bodies");p.returned=true;
    }
    if(p.frame==120) Require(kb::scene::PhysicsAssemblies::SimulateRagdoll(scene,p.rag,true,error),error);
    if(p.frame==180) {
        kb::scene::PhysicsAssemblyState state;Require(kb::scene::PhysicsAssemblies::State(scene,p.cloth,state,error)&&state.vertices.size()==64,"cloth vertices unavailable");
        Require(std::abs(state.vertices[0].z)<.0001F&&state.vertices.back().z>.2F,"cloth did not deform around pinned vertices");
        const auto owner=scene.Transforms().Get(p.surface);float low=100;for(const auto vertex:state.vertices) low=std::min(low,(owner.worldPosition+kb::math::Rotate(owner.worldRotation,vertex)).y);
        Require(low>-.08F&&low<.15F,"cloth did not collide with the floor");Require(p.bound&&p.returned,"ragdoll handoff checks did not run");
        const std::array args{kb::script::ScriptFunctionArgument{"message",kb::script::ScriptValue{std::string{"native-physics-assemblies PASS"}}}};
        Require(c->CallFunction("Log",args).Succeeded(),"native log failed");
    }
}
KB_NATIVE_SCRIPT_PLUGIN_EXPORT bool kb_register_native_scripts(kb::script::NativeScriptPluginApi* api) {
    ecs_os_set_api_defaults();return api&&api->version==kb::script::kNativeScriptPluginApiVersion&&api->registerLifecycle&&
        api->registerLifecycle(api->user,"project.NativePhysicsProbe",kb::script::ScriptLifecycleEvent::Ready,&Ready)&&
        api->registerLifecycle(api->user,"project.NativePhysicsProbe",kb::script::ScriptLifecycleEvent::Tick,&Tick);
}
''', encoding='utf-8')
    descriptor = project / 'Assets/Scripts/NativePhysicsProbe.native'
    descriptor.write_text('# @asset /Game/Physics/Rig.kbphysics\n# @asset /Game/Physics/Cloth.kbphysics\n' + descriptor.read_text(encoding='utf-8'), encoding='utf-8')
    cli('script-build', '--project', project, '--script', '/Game/Scripts/NativePhysicsProbe.native')
    cli('scene-attach', '--project', project, '--scene', '/Game/Scenes/Main.21kbscene', '--node', 'Rig', '--script', '/Game/Scripts/NativePhysicsProbe.native')
    cli('script-new', '--project', project, '--name', 'LuaPhysicsProbe', '--language', 'lua')
    annotations = ''.join('-- @asset /Game/Physics/' + name + '.kbphysics\n' for name in ('Car', 'Breakable', 'Float'))
    (project / 'Assets/Scripts/LuaPhysicsProbe.lua').write_text(annotations + r'''local frame=0
local car,fracture,water
local buoyant,breakable
local firstCar,turned,waterBefore
local function pose(entity) return CallFunction("Transform.WorldPose",{entity=entity}) end
function Ready(self) frame=0 end
function Tick(self,dt)
 frame=frame+1
 if frame==15 then
  buoyant=World.FindByName("Float");breakable=World.FindByName("Breakable")
  car=assert(Physics.AssemblyLoad(self.entity,"/Game/Physics/Car.kbphysics"))
  fracture=assert(Physics.AssemblyLoad(breakable,"/Game/Physics/Breakable.kbphysics"))
  water=assert(Physics.AssemblyLoad(buoyant,"/Game/Physics/Float.kbphysics"))
  firstCar=pose(self.entity)
  assert(Physics.AssemblyDrive(car,.6,0,0))
 elseif frame==70 then
  assert(Physics.AssemblyFracture(fracture,4,3,0))
 elseif frame==90 then
  local p=pose(self.entity)
  assert(Physics.AssemblyMetric(car,"ready")==1 and Physics.AssemblyMetric(car,"contacts")>=1,"vehicle has no wheel contacts")
  assert(p.posZ>firstCar.posZ+.1 and Physics.AssemblyMetric(car,"speed")>.2,"vehicle did not accelerate")
  assert(Physics.AssemblyDrive(car,.6,.3,0))
 elseif frame==150 then
  assert(Physics.AssemblyMetric(water,"submerged")==1,"body never displaced water")
  waterBefore=pose(buoyant).posY
  assert(waterBefore>.55 and waterBefore<1.4,"body did not float at the authored water plane")
  assert(Physics.AssemblyWaterLevel(water,2))
 elseif frame==180 then
  local p=pose(self.entity);turned=p.posX
  assert(math.abs(p.posX-firstCar.posX)>.1,"steering did not change the vehicle path")
  assert(Physics.AssemblyDrive(car,0,0,1))
  assert(Physics.AssemblyMetric(fracture,"fractured")==1 and Physics.AssemblyMetric(fracture,"entityCount")==8,"fracture did not create physical cells")
  local low,high=100,-100
  for i=0,7 do
   local piece=Physics.AssemblyEntity(fracture,i);local q=pose(piece)
   assert(q.found and q.posY<1.2,"fracture piece did not fall")
   low=math.min(low,q.posX);high=math.max(high,q.posX)
  end
  assert(high-low>1.1,"fracture pieces did not move independently")
 elseif frame==280 then
  assert(pose(buoyant).posY>waterBefore+.3,"body did not react to deeper immersion")
  assert(turned~=nil)
  Log("lua-physics-assemblies PASS")
 end
end
''', encoding='utf-8')
    cli('scene-attach', '--project', project, '--scene', '/Game/Scenes/Main.21kbscene', '--node', 'Car', '--script', '/Game/Scripts/LuaPhysicsProbe.lua')
    initial = root / 'EditorSource'
    shutil.copytree(project, initial, ignore=shutil.ignore_patterns('Saved'))
    steps = [dict(op='configure_runtime_rendering', width=640, height=360),
             dict(op='open_scene', path='Assets/Scenes/Main.21kbscene'), dict(op='save_scene'),
             dict(op='open_scene', path='Assets/Scenes/Main.21kbscene'), dict(op='play'),
             dict(op='step', frames=30), dict(op='capture_runtime', checkpoint='early', require_non_uniform=True),
             dict(op='step', frames=270), dict(op='capture_runtime', checkpoint='late', require_non_uniform=True),
             dict(op='assert_console', contains='native-physics-assemblies PASS'),
             dict(op='assert_console', contains='lua-physics-assemblies PASS'), dict(op='assert_no_errors'), dict(op='stop')]
    scenario = root / 'scenario.json'
    scenario.write_text(json.dumps(dict(schema='21kb.editor-automation/v1', initial_project=str(next(initial.glob('*.21kbproject'))), steps=steps)), encoding='utf-8')
    run(args.editor, '--selftest-scenario', scenario, '--selftest-root', root / 'Editor', '--selftest-task', 'Physics')
    sys.path.insert(0, str(args.engine_root.resolve() / 'scripts'))
    from png_readback import read_png
    early, late = [read_png(root / f'Editor/Physics/automation/screenshots/{name}.png') for name in ('early', 'late')]
    cloth_pixels = sum(max(abs(a-b) for a,b in zip(early[y][x],late[y][x])) > 3 for y in range(140, 300) for x in range(195, 260))
    assert cloth_pixels > 30, ('deforming cloth did not update GPU geometry', cloth_pixels)
    source = run(args.player, '--project=' + str(next(project.glob('*.21kbproject'))), '--headless', '--frames=300', '--uncapped',
        '--width=640', '--height=360', '--profile-fixed-step', '--profile-file=' + str(root / 'source.csv'))
    assert 'native-physics-assemblies PASS' in source and 'lua-physics-assemblies PASS' in source and 'errors=0' in source, source
    package = root / 'Package'
    package.mkdir()
    run(args.cooker, '--project', project, '--target', 'Windows.x64', '--output', package / 'Game.kbpack',
        '--engine-root', args.engine_root, '--shaderc', args.shaderc, '--cache', root / 'CookCache', '--runtime-modules-output', package / 'RuntimeModules')
    for plugin in args.plugin:
        shutil.copy2(plugin, package / plugin.name)
    project.rename(root / 'SourceUnavailable')
    initial.rename(root / 'EditorSourceUnavailable')
    output = run(args.player, '--project=' + str(package / 'Game.kbpack'), '--headless', '--frames=300', '--uncapped',
        '--width=640', '--height=360', '--profile-fixed-step', '--profile-file=' + str(root / 'package.csv'),
        '--screenshot-frame=300', '--screenshot-file=' + str(root / 'packaged.png'))
    assert 'native-physics-assemblies PASS' in output and 'lua-physics-assemblies PASS' in output and 'errors=0' in output and 'shutdown=clean' in output, output
    (root / 'result.json').write_text(json.dumps(dict(result='PASS', clothGpuPixels=cloth_pixels, nativeAndLua=True,
        ragdollPoseRoundTrip=True, clothDeformationAndContact=True, wheelContactsAndSteering=True,
        physicalFragments=8, buoyancyRespondsToImmersion=True, sourceUnavailable=True), indent=2), encoding='utf-8')
    print(root)


if __name__ == '__main__':
    main()
