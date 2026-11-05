"""Exercise scene geometry, sampled SDF and heightfield collisions through the particle provider."""
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
    root = Path(tempfile.mkdtemp(prefix='ParticleGeometry', dir=args.test_root)).resolve()
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
    entity('Camera', '0,2,-11', 'Camera', look_at='0,1,0')
    entity('Solid', '-3,0.4,0', 'Cube', scale='2,0.8,2', body='Static')
    entity('DistanceFieldVisual', '0,0.5,0', 'Sphere', scale='1.6,1.6,1.6')
    entity('NativeHost')
    entity('LuaHost')
    entity('Ambient')
    component('Ambient', 'Ambient Radiance', action='add')
    component('Ambient', 'Ambient Radiance', 'mode=0', 'color=0.18,0.18,0.18')
    entity('Sun', '0,5,-3', 'Directional Light', rotation='40,-20,0')
    cli('post-process-new', '--project', project, '--name', 'Look', '--set', 'autoExposureMetering=Manual',
        '--set', 'temporalAntiAliasingEnabled=0', '--set', 'bloomEnabled=0')
    cli('scene-post-process', '--project', project, '--profile', '/Game/PostProcess/Look.kbppfx')
    names = []
    for group, kind in enumerate(('PhysicsWorld', 'SignedDistanceGrid', 'Heightfield')):
        for variant in ('Bounce', 'Stick', 'Control'):
            name = kind + variant
            names.append(name)
            cli('vfx-new', '--project', project, '--folder', '/Game/Vfx', '--name', name)
            file = project / f'Assets/Vfx/{name}.kbvfx'
            records = dict(line.split(' ', 1) for line in file.read_text(encoding='utf-8').splitlines() if ' ' in line)
            header = file.read_text(encoding='utf-8').splitlines()[0]
            records.pop(header.split(' ', 1)[0], None)
            records = {k: v for k, v in records.items() if not k.startswith('effect.emitter[0].module[')}
            p = 'effect.emitter[0].'
            records.update({'effect.backendPolicy': 'CpuDeterministic' if variant == 'Control' else 'GpuVisualPreferred',
                'effect.durationSeconds': '4', 'effect.looping': 'false', p+'localPosition': f'{group*3-3 + {"Bounce":-.3,"Stick":.3,"Control":.9}[variant]} 2.8 0',
                p+'simulationSpace': 'World', p+'maxParticles': '8', p+'spawn.mode': 'Burst',
                p+'spawn.burstCount': '1', p+'spawn.burst[0].timeSeconds': '0', p+'spawn.burst[0].count': '1',
                p+'spawn.lifetimeMin': '4', p+'spawn.lifetimeMax': '4', p+'spawn.speedMin': '3', p+'spawn.speedMax': '3',
                p+'spawn.direction': '0 -1 0', p+'spawn.spreadDegrees': '0', p+'spawn.randomization': '0',
                p+'spawn.startSize': '.28', p+'spawn.startColor': ('1 .1 .05 1', '.05 1 .1 1', '.05 .2 1 1')[group],
                p+'moduleCount': '0' if variant == 'Control' else '1'})
            if variant != 'Control':
                m = p + 'module[0].'
                records.update({m+'id': '1', m+'type': 'CollisionGeometry', m+'enabled': 'true',
                    m+'payload.kind': kind, m+'payload.radius': '.07', m+'payload.restitution': '.6' if variant == 'Bounce' else '0',
                    m+'payload.friction': '.1' if variant == 'Bounce' else '1', m+'payload.maxEventsPerStep': '64'})
                g = m + 'payload.'
                if kind == 'PhysicsWorld':
                    records[g+'layerMask'] = '4294967295'
                else:
                    height = 7 if kind == 'SignedDistanceGrid' else 1
                    records.update({g+'origin': '-1.5 -1 -1.5' if height == 7 else '2 0 -1.5',
                        g+'cellSize': '.5 .5 .5', g+'width': '7', g+'height': str(height), g+'depth': '7', g+'sampleCount': str(49*height)})
                    for z in range(7):
                        for y in range(height):
                            for x in range(7):
                                value = math.sqrt((x*.5-1.5)**2+(y*.5-1.5)**2+(z*.5-1.5)**2)-.8 if height == 7 else .5+.2*math.sin(x*math.pi/6)*math.cos((z-3)*math.pi/9)
                                records[g+f'sample[{x+7*(y+height*z)}]'] = str(value)
            file.write_text(header + '\n' + '\n'.join(k+' '+v for k,v in records.items())+'\n', encoding='utf-8')
            shown = cli('vfx-show', '--project', project, '--effect', '/Game/Vfx/'+name+'.kbvfx')
            assert 'CollisionGeometry' in shown if variant != 'Control' else 'moduleCount 0' in shown
            if name == 'SignedDistanceGridBounce':
                before = file.read_bytes()
                error = cli('vfx-set', '--project', project, '--effect', '/Game/Vfx/'+name+'.kbvfx',
                    '--set', 'effect.backendPolicy=GpuVisualRequired', expected=1)
                assert 'CPU' in error and file.read_bytes() == before
                cli('vfx-set', '--project', project, '--effect', '/Game/Vfx/'+name+'.kbvfx',
                    '--set', 'effect.emitter[0].module[0].payload.restitution=0.75')
                assert 'payload.restitution 0.75' in file.read_text(encoding='utf-8')
    cli('script-new', '--project', project, '--name', 'NativeGeometryProbe', '--language', 'cpp')
    source = project / 'Source/NativeScripts/NativeGeometryProbe/NativeGeometryProbe.cpp'
    source.write_text(r'''#include "engine/script/NativeScriptPlugin.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/ParticleEffectAsset.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/ParticleEffectAssetValidation.hpp"
#include "engine/particles/ParticlePlayback.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <stdexcept>
extern "C" void ecs_os_set_api_defaults(void);
struct Probe {int frame=0;};static std::map<std::pair<std::uint64_t,std::uint64_t>,Probe> probes;
static void Require(bool test,const std::string& error){if(!test) throw std::runtime_error(error);}
static const std::array<const char*,3> kinds{"PhysicsWorld","SignedDistanceGrid","Heightfield"};
static kb::assets::AssetId Asset(kb::scene::Scene& scene,const std::string& name){
 const auto* metadata=scene.Assets().Manager().Registry().FindByPath("/Game/Vfx/"+name+".kbvfx");Require(metadata!=nullptr,"effect not packaged: "+name);return metadata->id;
}
static void Ready(kb::script::ScriptExecutionContext* c){probes[{c->GetScene().Id(),c->Self().Id()}]={};}
static void Tick(kb::script::ScriptExecutionContext* c){
 auto& s=c->GetScene();auto& p=probes[{s.Id(),c->Self().Id()}];++p.frame;
 if(p.frame==10){
  for(const auto* kind:kinds) for(const auto* variant:{"Bounce"}) {
   const auto result=kb::particles::ParticlePlayback::Create(s,Asset(s,std::string{kind}+variant).value,c->Self());Require(result.Succeeded(),"native particle create failed");
   Require(kb::particles::ParticlePlayback::Play(s,result.instanceId).Succeeded(),"native particle play failed");
  }
  auto loaded=s.Assets().Manager().Load<kb::scene::ParticleEffectAsset>(Asset(s,"SignedDistanceGridBounce"));Require(loaded.IsLoaded(),"SDF asset failed to load");
  auto bad=*loaded.Shared();auto& grid=std::get<kb::scene::ParticleCollisionGeometryModule>(bad.emitters[0].modules[0].payload);grid.samples.pop_back();
  Require(!kb::scene::ParticleEffectAssetValidator::ValidateStructure(bad).Succeeded(),"truncated grid was accepted");
  std::vector<kb::scene::ParticleEffectDiagnostic> diagnostics;Require(!kb::scene::ParticleEffectAssetIO::Serialize(bad,diagnostics).has_value(),"invalid grid was serialized");
 }
 if(p.frame==80){
  const auto snapshot=kb::particles::ParticlePlayback::ReadRenderSnapshot(s);Require(snapshot!=nullptr,"particle snapshot unavailable");
  for(const auto* kind:kinds){
   std::array<kb::particles::ParticleRenderRecord,3> particles;unsigned n=0;
   for(const auto* variant:{"Bounce","Stick","Control"}) {
    const auto id=Asset(s,std::string{kind}+variant).value;
    const auto rows=snapshot->Emitters();const auto row=std::find_if(rows.begin(),rows.end(),[&](const auto& e){return e.effectAssetId==id;});
    Require(row!=rows.end()&&row->particleCount==1,"particle output missing: "+std::string{kind}+variant+" rows="+std::to_string(rows.size())+" count="+std::to_string(row!=rows.end()?row->particleCount:0)+" instances="+std::to_string(kb::particles::ParticlePlayback::LiveInstanceIds(s).size()));
    Require(row->simulationPath==kb::particles::ParticleEmitterSimulationPath::Cpu,"geometry was routed to unsupported GPU simulation");
    if(n<2) Require(row->gpuFallbackReason==kb::particles::ParticleGpuFallbackReason::GeometryCollision,"geometry CPU reason was not published");
    particles[n++]=snapshot->Particles()[row->firstParticle];
   }
   Require(particles[0].position.y>particles[1].position.y+.3F&&particles[0].velocity.y>1,"restitution did not bounce: "+std::string{kind});
   Require(particles[1].position.y>.3F&&kb::math::Length(particles[1].velocity)<.05F,"inelastic contact failed: "+std::string{kind});
   if(std::string{kind}=="SignedDistanceGrid") Require(particles[1].position.y>1.15F&&particles[1].position.y<1.4F,"SDF samples did not define the contact surface");
   if(std::string{kind}=="Heightfield") Require(particles[1].position.y>.73F&&particles[1].position.y<.82F,"heightfield samples did not define the contact surface");
   Require(particles[2].position.y<0&&particles[2].velocity.y< -2.9F,"disabled collision control did not pass through: "+std::string{kind});
  }
  const std::array args{kb::script::ScriptFunctionArgument{"message",kb::script::ScriptValue{std::string{"native-particle-geometry PASS"}}}};
  Require(c->CallFunction("Log",args).Succeeded(),"native log failed");
 }
}
KB_NATIVE_SCRIPT_PLUGIN_EXPORT bool kb_register_native_scripts(kb::script::NativeScriptPluginApi* api){
 ecs_os_set_api_defaults();return api&&api->version==kb::script::kNativeScriptPluginApiVersion&&api->registerLifecycle&&
 api->registerLifecycle(api->user,"project.NativeGeometryProbe",kb::script::ScriptLifecycleEvent::Ready,&Ready)&&
 api->registerLifecycle(api->user,"project.NativeGeometryProbe",kb::script::ScriptLifecycleEvent::Tick,&Tick);
}
''', encoding='utf-8')
    descriptor = project / 'Assets/Scripts/NativeGeometryProbe.native'
    descriptor.write_text(''.join('# @asset /Game/Vfx/'+name+'.kbvfx\n' for name in names if name.endswith('Bounce'))+descriptor.read_text(encoding='utf-8'), encoding='utf-8')
    cli('script-build', '--project', project, '--script', '/Game/Scripts/NativeGeometryProbe.native')
    cli('scene-attach', '--project', project, '--scene', '/Game/Scenes/Main.21kbscene', '--node', 'NativeHost', '--script', '/Game/Scripts/NativeGeometryProbe.native')
    cli('script-new', '--project', project, '--name', 'LuaGeometryProbe', '--language', 'lua')
    lua = ''.join('-- @asset /Game/Vfx/'+name+'.kbvfx\n' for name in names if not name.endswith('Bounce'))
    lua += '''local frame=0
function Ready(self) frame=0 end
function Tick(self,dt)
 frame=frame+1
 if frame==10 then
  for _,kind in ipairs({"PhysicsWorld","SignedDistanceGrid","Heightfield"}) do
   for _,variant in ipairs({"Stick","Control"}) do
    local handle=assert(Particles.Create("/Game/Vfx/"..kind..variant..".kbvfx"))
    assert(Particles.Play(handle))
   end
  end
  Log("lua-particle-geometry PASS")
 end
end
'''
    (project / 'Assets/Scripts/LuaGeometryProbe.lua').write_text(lua, encoding='utf-8')
    cli('scene-attach', '--project', project, '--scene', '/Game/Scenes/Main.21kbscene', '--node', 'LuaHost', '--script', '/Game/Scripts/LuaGeometryProbe.lua')
    initial = root / 'EditorSource'
    shutil.copytree(project, initial, ignore=shutil.ignore_patterns('Saved'))
    scenario = root / 'scenario.json'
    scenario.write_text(json.dumps(dict(schema='21kb.editor-automation/v1', initial_project=str(next(initial.glob('*.21kbproject'))), steps=[
        dict(op='configure_runtime_rendering', width=640, height=360), dict(op='open_scene', path='Assets/Scenes/Main.21kbscene'),
        dict(op='save_scene'), dict(op='open_scene', path='Assets/Scenes/Main.21kbscene'), dict(op='play'),
        dict(op='step', frames=20), dict(op='capture_runtime', checkpoint='early', require_non_uniform=True),
        dict(op='step', frames=60), dict(op='capture_runtime', checkpoint='late', require_non_uniform=True),
        dict(op='assert_console', contains='native-particle-geometry PASS'), dict(op='assert_console', contains='lua-particle-geometry PASS'),
        dict(op='assert_no_errors'), dict(op='stop')])), encoding='utf-8')
    run(args.editor, '--selftest-scenario', scenario, '--selftest-root', root / 'Editor', '--selftest-task', 'Geometry')
    sys.path.insert(0, str(args.engine_root.resolve() / 'scripts'))
    from png_readback import read_png
    early, late = [read_png(root / f'Editor/Geometry/automation/screenshots/{name}.png') for name in ('early', 'late')]
    changes = []
    for left, right in ((175, 265), (280, 365), (390, 485)):
        changed=sum(max(abs(a-b) for a,b in zip(early[y][x][:3],late[y][x][:3]))>8 for y in range(90,300) for x in range(left,right))
        assert changed>20, ('particle geometry GPU result unchanged',left,changed)
        changes.append(changed)
    output=run(args.player,'--project='+str(next(project.glob('*.21kbproject'))),'--headless','--frames=100','--uncapped','--width=640','--height=360',
        '--profile-fixed-step','--profile-file='+str(root/'source.csv'))
    assert 'native-particle-geometry PASS' in output and 'lua-particle-geometry PASS' in output and 'errors=0' in output,output
    package=root/'Package';package.mkdir()
    run(args.cooker,'--project',project,'--target','Windows.x64','--output',package/'Game.kbpack','--engine-root',args.engine_root,
        '--shaderc',args.shaderc,'--cache',root/'CookCache','--runtime-modules-output',package/'RuntimeModules')
    for plugin in args.plugin:shutil.copy2(plugin,package/plugin.name)
    project.rename(root/'SourceUnavailable');initial.rename(root/'EditorSourceUnavailable')
    output=run(args.player,'--project='+str(package/'Game.kbpack'),'--headless','--frames=100','--uncapped','--width=640','--height=360',
        '--profile-fixed-step','--profile-file='+str(root/'package.csv'))
    assert 'native-particle-geometry PASS' in output and 'lua-particle-geometry PASS' in output and 'errors=0' in output and 'shutdown=clean' in output,output
    (root/'result.json').write_text(json.dumps(dict(result='PASS',geometryKinds=['PhysicsWorld','SignedDistanceGrid','Heightfield'],
        gpuChangedPixels=changes,restitutionAndDisabledControls=True,invalidGridAndGpuRequiredRejected=True,nativeAndLua=True,sourceUnavailable=True),indent=2),encoding='utf-8')
    print(root)


if __name__ == '__main__':
    main()
