"""Qualify HRTF and first-order AmbiX through real offline PCM in the editor and player."""
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
    root = Path(tempfile.mkdtemp(prefix='SpatialAudio', dir=args.test_root)).resolve()
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
    entity('Camera','0,0,-6','Camera')
    entity('Listener')
    component('Listener','Audio Listener',action='add')
    entity('NativeHost');entity('LuaHost');entity('EmitterLeft','-1,0,0')
    import wave,struct
    audio=project/'Assets/Audio';audio.mkdir(exist_ok=True)
    for name,channels in [('Impulse',1),('LeftField',4)]:
        with wave.open(str(audio/(name+'.wav')),'wb') as wav:
            wav.setnchannels(channels);wav.setsampwidth(2);wav.setframerate(48000)
            frames=[]
            for frame in range(32768):
                signal=12000 if frame==128 else 0
                frames.extend([signal] if channels==1 else [signal,signal,0,0])
            wav.writeframes(struct.pack('<'+'h'*len(frames),*frames))
    cli('script-new','--project',project,'--name','NativeSpatialProbe','--language','cpp')
    (project/'Source/NativeScripts/NativeSpatialProbe/NativeSpatialProbe.cpp').write_text(r'''#include "engine/script/NativeScriptPlugin.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/audio/SpatialAudioPlayback.hpp"
#include <array>
#include <map>
#include <cmath>
#include <stdexcept>
#include <algorithm>
extern "C" void ecs_os_set_api_defaults(void);
struct Stats{double left=0,right=0;int leftPeak=0,rightPeak=0,leftTaps=0,rightTaps=0;};
struct Probe{int frame=0;bool native=false,luaHrtf=false,ambi=false,rotated=false;Stats hrtf,field,turned;};
static std::map<std::pair<std::uint64_t,std::uint64_t>,Probe> probes;
static void Require(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
static kb::audio::AudioOfflinePcm Read(kb::scene::Scene& s){auto pcm=kb::audio::AudioPlayback::RenderOffline(s,4096);Require(pcm.succeeded&&pcm.channels==2&&pcm.sampleRate==48000&&pcm.frames==4096,pcm.error.empty()?"offline spatial PCM unavailable":pcm.error);return pcm;}
static Stats Analyze(const kb::audio::AudioOfflinePcm& pcm){Stats a;float l=0,r=0;for(std::size_t i=0;i<pcm.frames;++i){const float left=pcm.samples[i*2],right=pcm.samples[i*2+1];Require(std::isfinite(left)&&std::isfinite(right),"spatial PCM is not finite");a.left+=left*left;a.right+=right*right;if(std::abs(left)>l){l=std::abs(left);a.leftPeak=static_cast<int>(i);}if(std::abs(right)>r){r=std::abs(right);a.rightPeak=static_cast<int>(i);}if(std::abs(left)>.000001F)++a.leftTaps;if(std::abs(right)>.000001F)++a.rightTaps;}return a;}
static void Left(const Stats& a,const char* stage){Require(a.left>a.right*1.1&&a.left>.000001,std::string{stage}+" expected left directional energy: "+std::to_string(a.left)+" / "+std::to_string(a.right));}
static void Right(const Stats& a,const char* stage){Require(a.right>a.left*1.1&&a.right>.000001,std::string{stage}+" expected right directional energy: "+std::to_string(a.left)+" / "+std::to_string(a.right));}
static kb::assets::AssetId Asset(kb::scene::Scene& s,const char* name){const auto* a=s.Assets().Manager().Registry().FindByPath(std::string{"/Game/Audio/"}+name+".wav");Require(a!=nullptr,"audio asset missing");return a->id;}
static kb::audio::AudioPlayResult Play(kb::scene::Scene& s,const char* name,kb::audio::SpatialAudioMode mode,float x=0){kb::audio::AudioPlayDesc d;d.clipAssetId=Asset(s,name).value;d.volume=.2F;d.dopplerFactor=0;d.attenuationModel=kb::audio::AudioAttenuationModel::None;d.position={x,0,0};return kb::audio::SpatialAudioPlayback::Play(s,d,mode);}
static void Turn(kb::script::ScriptExecutionContext* c,float degrees){const auto found=c->CallFunction("World.FindByName",std::array{kb::script::ScriptFunctionArgument{"name",kb::script::ScriptValue{std::string{"Listener"}}}});Require(found.Succeeded(),"listener lookup failed");const kb::scene::SceneEntity entity{found.Output("entity")->AsUInt64()};auto t=c->GetScene().Transforms().Get(entity);t.localRotation=kb::math::FromEulerDegrees({0,degrees,0});c->GetScene().Transforms().Set(entity,t);c->GetScene().Runtime().SynchronizeTransforms();}
static void Ready(kb::script::ScriptExecutionContext* c){probes[{c->GetScene().Id(),c->Self().Id()}]={};}
static void Tick(kb::script::ScriptExecutionContext* c){auto& s=c->GetScene();auto& p=probes[{s.Id(),c->Self().Id()}];++p.frame;
 if(p.frame==5)Require(kb::audio::AudioPlayback::ConfigureOffline(s),"offline audio configure failed");
 if(p.frame==10){
  auto reference=Play(s,"Impulse",kb::audio::SpatialAudioMode::Panning,-1);Require(reference.Succeeded(),reference.error);auto pan=Analyze(Read(s));kb::audio::AudioPlayback::StopAll(s);
  auto voice=Play(s,"Impulse",kb::audio::SpatialAudioMode::Binaural,-1);Require(voice.Succeeded(),voice.error);auto first=Read(s);p.hrtf=Analyze(first);Left(p.hrtf,"native HRTF");
  Require(p.hrtf.leftTaps>pan.leftTaps+16&&p.hrtf.rightTaps>16&&std::abs(p.hrtf.leftPeak-p.hrtf.rightPeak)>=2,"HRTF lacks impulse filtering or interaural delay");
  const auto bad=Play(s,"LeftField",kb::audio::SpatialAudioMode::Binaural,-1);Require(!bad.Succeeded()&&kb::audio::AudioPlayback::IsVoicePlaying(s,voice.voiceId),"wrong channel request mutated a live voice");
  Require(kb::audio::AudioPlayback::SeekVoice(s,voice.voiceId,0),"HRTF seek failed");const auto repeated=Read(s);float difference=0;for(std::size_t i=0;i<first.samples.size();++i) difference=std::max(difference,std::abs(first.samples[i]-repeated.samples[i]));Require(difference<.00001F,"HRTF seek did not reset convolution state: "+std::to_string(difference));
  Require(kb::audio::AudioPlayback::PauseVoice(s,voice.voiceId),"HRTF pause failed");Require(Analyze(Read(s)).left<1.0e-12,"paused HRTF still produced PCM");Require(kb::audio::AudioPlayback::ResumeVoice(s,voice.voiceId),"HRTF resume failed");
  kb::audio::AudioPlayback::StopAll(s);auto right=Play(s,"Impulse",kb::audio::SpatialAudioMode::Binaural,1);Require(right.Succeeded(),right.error);Right(Analyze(Read(s)),"native right HRTF");kb::audio::AudioPlayback::StopAll(s);
  Turn(c,180);auto rotated=Play(s,"Impulse",kb::audio::SpatialAudioMode::Binaural,-1);Require(rotated.Succeeded(),rotated.error);Right(Analyze(Read(s)),"rotated listener HRTF");kb::audio::AudioPlayback::StopAll(s);Turn(c,0);
  kb::audio::AudioPlayDesc unsupported;unsupported.clipAssetId=Asset(s,"Impulse").value;Require(!kb::audio::SpatialAudioPlayback::Play(s,unsupported,kb::audio::SpatialAudioMode::Binaural).Succeeded(),"unsupported Doppler was silently ignored");p.native=true;
 }
 if(p.frame==31){Left(Analyze(Read(s)),"Lua HRTF");kb::audio::AudioPlayback::StopAll(s);p.luaHrtf=true;}
 if(p.frame==41){p.field=Analyze(Read(s));Left(p.field,"AmbiX field");kb::audio::AudioPlayback::StopAll(s);p.ambi=true;}
 if(p.frame==49)Turn(c,180);
 if(p.frame==51){p.turned=Analyze(Read(s));Right(p.turned,"rotated AmbiX field");kb::audio::AudioPlayback::StopAll(s);p.rotated=true;}
 if(p.frame==70){Require(p.native&&p.luaHrtf&&p.ambi&&p.rotated,"spatial phases incomplete");const std::string message="native-spatial-audio PASS taps="+std::to_string(p.hrtf.leftTaps)+"/"+std::to_string(p.hrtf.rightTaps)+" delay="+std::to_string(std::abs(p.hrtf.leftPeak-p.hrtf.rightPeak))+" ambi="+std::to_string(p.field.left)+"/"+std::to_string(p.field.right)+" rotated="+std::to_string(p.turned.left)+"/"+std::to_string(p.turned.right);Require(c->CallFunction("Log",std::array{kb::script::ScriptFunctionArgument{"message",kb::script::ScriptValue{message}}}).Succeeded(),"spatial log failed");}
}
KB_NATIVE_SCRIPT_PLUGIN_EXPORT bool kb_register_native_scripts(kb::script::NativeScriptPluginApi* api){ecs_os_set_api_defaults();return api&&api->version==kb::script::kNativeScriptPluginApiVersion&&api->registerLifecycle(api->user,"project.NativeSpatialProbe",kb::script::ScriptLifecycleEvent::Ready,&Ready)&&api->registerLifecycle(api->user,"project.NativeSpatialProbe",kb::script::ScriptLifecycleEvent::Tick,&Tick);}
''',encoding='utf-8')
    descriptor=project/'Assets/Scripts/NativeSpatialProbe.native'
    descriptor.write_text('# @asset /Game/Audio/Impulse.wav\n# @asset /Game/Audio/LeftField.wav\n'+descriptor.read_text(encoding='utf-8'),encoding='utf-8')
    cli('script-build','--project',project,'--script','/Game/Scripts/NativeSpatialProbe.native')
    cli('scene-attach','--project',project,'--scene','/Game/Scenes/Main.21kbscene','--node','NativeHost','--script','/Game/Scripts/NativeSpatialProbe.native')
    cli('script-new','--project',project,'--name','LuaSpatialProbe','--language','lua')
    (project/'Assets/Scripts/LuaSpatialProbe.lua').write_text('''-- @asset /Game/Audio/Impulse.wav
-- @asset /Game/Audio/LeftField.wav
local frame=0
function Ready(self)frame=0 end
function Tick(self,dt)
 frame=frame+1
 if frame==30 then assert(Audio.Play("/Game/Audio/Impulse.wav",{entity=World.FindByName("EmitterLeft"),volume=.2,dopplerFactor=0,attenuationModel=0,spatialization=1})) end
 if frame==40 or frame==50 then assert(Audio.Play("/Game/Audio/LeftField.wav",{entity=self.entity,volume=.2,dopplerFactor=0,attenuationModel=0,spatialization=2})) end
 if frame==70 then Log("lua-spatial-audio PASS") end
end
''',encoding='utf-8')
    cli('scene-attach','--project',project,'--scene','/Game/Scenes/Main.21kbscene','--node','LuaHost','--script','/Game/Scripts/LuaSpatialProbe.lua')
    initial=root/'EditorSource';shutil.copytree(project,initial,ignore=shutil.ignore_patterns('Saved'))
    scenario=root/'scenario.json'
    scenario.write_text(json.dumps(dict(schema='21kb.editor-automation/v1',initial_project=str(next(initial.glob('*.21kbproject'))),steps=[
        dict(op='open_scene',path='Assets/Scenes/Main.21kbscene'),dict(op='save_scene'),dict(op='open_scene',path='Assets/Scenes/Main.21kbscene'),dict(op='play'),dict(op='step',frames=80),
        dict(op='assert_console',contains='native-spatial-audio PASS'),dict(op='assert_console',contains='lua-spatial-audio PASS'),dict(op='assert_no_errors'),dict(op='stop')])),encoding='utf-8')
    run(args.editor,'--selftest-scenario',scenario,'--selftest-root',root/'Editor','--selftest-task','SpatialAudio')
    output=run(args.player,'--project='+str(next(project.glob('*.21kbproject'))),'--headless','--frames=100','--uncapped','--profile-fixed-step','--profile-file='+str(root/'source.csv'))
    assert 'native-spatial-audio PASS' in output and 'lua-spatial-audio PASS' in output and 'errors=0' in output,output
    package=root/'Export'
    run(Path(sys.executable),args.engine_root/'scripts/package_game.py','--project',next(project.glob('*.21kbproject')),'--target','Windows.x64',
        '--configuration','Development','--output',package,'--engine-root',args.engine_root,'--build-root',args.player.resolve().parents[1],
        '--product-name','SampleScene','--publisher','Engine Samples','--version','0.1.0','--executable-name','SamplePlayer')
    assert (package/'phonon.dll').is_file()
    sbom=json.loads((package/'sbom.cdx.json').read_text(encoding='utf-8'));assert any(c['name']=='Steam Audio' for c in sbom['components'])
    isolated=root.parent/(root.name+'Package');assert isolated.resolve().parent==args.test_root.resolve();shutil.copytree(package,isolated)
    unavailable=root.parent/(root.name+'SourceUnavailable');assert root.parent==args.test_root.resolve() and unavailable.resolve().parent==args.test_root.resolve()
    original=root;root.rename(unavailable);root=unavailable;assert not original.exists()
    output=run(isolated/'SamplePlayer.exe','--headless','--frames=100','--uncapped')
    assert 'native-spatial-audio PASS' in output and 'lua-spatial-audio PASS' in output and 'errors=0' in output and 'shutdown=clean' in output,output
    (root/'result.json').write_text(json.dumps(dict(result='PASS',nativeAndLua=True,hrtfImpulseFilteringAndDelay=True,ambixRotation=True,seekPauseResumeAndChannelErrors=True,
        sourceUnavailable=True,isolatedPlayer=str(isolated/'SamplePlayer.exe'),exportedSdkAndSbom=True,pcmEvidence=[line for line in output.splitlines() if 'native-spatial-audio PASS' in line]),indent=2),encoding='utf-8')
    print(root)


if __name__=='__main__':
    main()
