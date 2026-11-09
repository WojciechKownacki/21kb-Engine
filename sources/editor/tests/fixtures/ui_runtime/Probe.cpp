#include "engine/script/NativeScriptPlugin.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneUI.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneUIComponents.hpp"
#include "engine/scene/SceneAccessibility.hpp"
#include "engine/scene/SceneVideoPlayers.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/ui/DynamicImageAsset.hpp"
#include <array>
#include <map>
#include <cmath>
#include <stdexcept>
extern "C" void ecs_os_set_api_defaults(void);
using namespace kb::scene;
struct Probe {int frame=0;SceneEntity button{},video{},luaVideo{};std::uint64_t speech=0;bool semantic=false,pcm=false,nativeVideo=false,lua=false,lifecycle=false;};
static std::map<std::pair<std::uint64_t,std::uint64_t>,Probe> probes;
static void Require(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
static SceneEntity Find(kb::script::ScriptExecutionContext* c,const char* name){auto r=c->CallFunction("World.FindByName",std::array{kb::script::ScriptFunctionArgument{"name",kb::script::ScriptValue{std::string{name}}}});Require(r.Succeeded(),"entity lookup failed");auto entity=SceneEntity{r.Output("entity")->AsUInt64()};Require(entity.IsValid(),name);return entity;}
static void UI(Scene& s){Require(s.UI().Update(640,360,{},0),"UI update refused");}
static kb::audio::AudioOfflinePcm PCM(Scene& s,unsigned frames){auto pcm=kb::audio::AudioPlayback::RenderOffline(s,frames);Require(pcm.succeeded,pcm.error);return pcm;}
static double Energy(const std::vector<float>& samples){double sum=0;for(float v:samples){Require(std::isfinite(v),"PCM is nonfinite");sum+=v*v;}return sum;}
static void Color(Scene& s,SceneEntity entity,bool blue){const auto* raw=s.Components().UI().TryGet<UIRawImage>(entity);Require(raw!=nullptr,"video raw image missing");const auto image=s.Assets().Manager().AcquireLoaded<DynamicImageAsset>(kb::assets::AssetId{raw->imageAssetId});Require(image.IsLoaded()&&image->rgba.size()>100,"decoded RGBA missing");const auto i=(static_cast<std::size_t>(image->height/2)*image->width+image->width/2)*4;Require(blue?image->rgba[i+2]>image->rgba[i]+100:image->rgba[i]>image->rgba[i+2]+100,"decoded video color is wrong");}
static void Ready(kb::script::ScriptExecutionContext* c){probes[{c->GetScene().Id(),c->Self().Id()}]={};}
static void Tick(kb::script::ScriptExecutionContext* c){auto& s=c->GetScene();auto& p=probes[{s.Id(),c->Self().Id()}];++p.frame;std::string error;
 if(p.frame==1)Require(kb::audio::AudioPlayback::ConfigureOffline(s),"offline UI audio unavailable");
 if(p.frame==5){p.button=Find(c,"Action");p.video=Find(c,"Video");p.luaVideo=Find(c,"LuaVideo");
  const auto* a=s.Components().UI().TryGet<UIAccessibility>(p.button);Require(a&&UIAccessibilityLabel(*a)=="Start sample","saved semantic annotation missing");
  const auto label=Find(c,"Label");const auto* text=s.Components().UI().TryGet<UIText>(label);Require(text&&text->fontRenderMode==UITextFontRenderMode::MultiChannelSignedDistanceField,"saved MSDF mode missing");
  UI(s);Require(SceneAccessibility::Focus(s,p.button),"semantic focus failed");auto tree=SceneAccessibility::Read(s);Require(tree.error.empty()&&tree.focused==p.button,"semantic tree focus failed");
  bool found=false,secret=false;for(const auto& n:tree.nodes){if(n.entity==p.button){Require(n.name=="Start sample"&&n.focusable&&n.enabled&&n.role==UIAccessibilityRole::Button,"semantic button fields wrong");found=true;}if(n.password){Require(n.value.empty()&&SceneAccessibility::Describe(n).find("Private sample value")==std::string::npos,"password leaked into accessibility tree");secret=true;}}
  Require(found&&secret&&SceneAccessibility::Invoke(s,p.button),"semantic invoke failed");p.semantic=true;
  p.speech=SceneAccessibility::SpeakFocused(s,error);Require(p.speech!=0,error);Require(SceneAccessibility::WaitForSpeech(s,p.speech,30000),SceneAccessibility::ReadSpeech(s,p.speech).error);auto speech=SceneAccessibility::ReadSpeech(s,p.speech);Require(speech.samples&&speech.samples->size()>2000&&Energy(*speech.samples)>.001,"SAPI produced no speech PCM");Require(Energy(PCM(s,4800).samples)>.000001,"speech did not reach shared audio mixer");p.pcm=true;Require(kb::audio::AudioPlayback::StopVoice(s,speech.voiceId),"speech stop failed");
 }
 if(p.frame==10){Require(SceneVideoPlayers::Play(s,p.video,"/Game/UI/Clip.21kb",false,error),error);Color(s,p.video,false);auto id=SceneVideoPlayers::Read(s,p.video).voiceId;Require(!SceneVideoPlayers::Play(s,p.video,"/Game/UI/Font.21kb",false,error)&&SceneVideoPlayers::Read(s,p.video).voiceId==id,"invalid video replaced a live player");}
 if(p.frame==12){Require(Energy(PCM(s,12000).samples)>.01,"decoded video audio is silent");UI(s);auto r=SceneVideoPlayers::Read(s,p.video);Require(r.error.empty()&&r.audioSeconds>.24&&r.audioSeconds<.28&&r.videoSeconds<=r.audioSeconds+.04&&r.audioSeconds-r.videoSeconds<.08,"video is not synchronized to the PCM cursor");}
 if(p.frame==13){Require(SceneVideoPlayers::Pause(s,p.video),"video pause failed");auto before=SceneVideoPlayers::Read(s,p.video);Require(Energy(PCM(s,4800).samples)<1e-10,"paused video still emitted PCM");UI(s);auto after=SceneVideoPlayers::Read(s,p.video);Require(after.paused&&after.audioSeconds==before.audioSeconds&&after.frameRevision==before.frameRevision,"paused video clock moved");}
 if(p.frame==14){Require(SceneVideoPlayers::Resume(s,p.video),"video resume failed");Require(Energy(PCM(s,12000).samples)>.01,"resumed video lost audio");UI(s);Require(SceneVideoPlayers::Seek(s,p.video,.25,error),error);Require(SceneVideoPlayers::Pause(s,p.video),"pause before capture failed");Color(s,p.video,false);}
 if(p.frame==30){Require(!SceneVideoPlayers::Seek(s,p.video,-1,error),"negative video seek accepted");Require(SceneVideoPlayers::Seek(s,p.video,1.25,error),error);Color(s,p.video,true);auto r=SceneVideoPlayers::Read(s,p.video);Require(r.error.empty()&&r.videoSeconds>=1.2&&r.videoSeconds<=1.3&&r.paused,"video seek did not land on the audio timeline");p.nativeVideo=true;}
 if(p.frame==35){SceneVideoPlayers::Stop(s,p.video);Require(s.Components().UI().TryGet<UIRawImage>(p.video)->imageAssetId==0,"video stop did not restore authored image");}
 if(p.frame==41){Require(Energy(PCM(s,4800).samples)>.01,"Lua video did not reach mixer");UI(s);auto r=SceneVideoPlayers::Read(s,p.luaVideo);Require(r.available&&r.playing&&r.audioSeconds>.09,"Lua video backend was not used");}
 if(p.frame==51){UI(s);Color(s,p.luaVideo,true);auto r=SceneVideoPlayers::Read(s,p.luaVideo);Require(r.paused&&r.videoSeconds>=1.4&&r.error.empty(),"Lua seek/pause not shared with C++");p.lua=true;}
 if(p.frame==55){Require(SceneVideoPlayers::Resume(s,p.luaVideo),"loop resume failed");Require(SceneVideoPlayers::Seek(s,p.luaVideo,1.8,error),error);Require(Energy(PCM(s,24000).samples)>.01,"loop audio missing");UI(s);Color(s,p.luaVideo,false);auto r=SceneVideoPlayers::Read(s,p.luaVideo);Require(r.playing&&r.audioSeconds>.2&&r.audioSeconds<.5&&r.error.empty(),"video did not wrap with the looping audio cursor");SceneVideoPlayers::Stop(s,p.luaVideo);}
 if(p.frame==56){Require(SceneVideoPlayers::Play(s,p.video,"/Game/UI/Clip.21kb",false,error),error);Require(SceneVideoPlayers::Seek(s,p.video,1.8,error),error);static_cast<void>(PCM(s,24000));static_cast<void>(PCM(s,1));UI(s);auto r=SceneVideoPlayers::Read(s,p.video);Require(r.ended&&!r.playing&&r.error.empty(),"natural video EOF after a frame hitch was misclassified: "+r.error);Color(s,p.video,true);SceneVideoPlayers::Stop(s,p.video);}
 if(p.frame==60){Require(SceneVideoPlayers::Play(s,p.luaVideo,"/Game/UI/Clip.21kb",false,error),error);const auto id=s.Components().UI().TryGet<UIRawImage>(p.luaVideo)->imageAssetId;s.Entities().Destroy(p.luaVideo);UI(s);Require(!SceneVideoPlayers::Read(s,p.luaVideo).available&&!s.Assets().Manager().Registry().Find(kb::assets::AssetId{id}),"destroyed video owner retained its generated frame asset");p.lifecycle=true;}
 if(p.frame==70){Require(p.semantic&&p.pcm&&p.nativeVideo&&p.lua&&p.lifecycle,"UI phases incomplete");Require(c->CallFunction("Log",std::array{kb::script::ScriptFunctionArgument{"message",kb::script::ScriptValue{std::string{"native-runtime-ui PASS semantic focus invoke speechPCM videoPCM cursorSync seek pause resume loop EOF ownerCleanup Lua"}}}}).Succeeded(),"native UI log failed");}
}
KB_NATIVE_SCRIPT_PLUGIN_EXPORT bool kb_register_native_scripts(kb::script::NativeScriptPluginApi* api){ecs_os_set_api_defaults();return api&&api->version==kb::script::kNativeScriptPluginApiVersion&&api->registerLifecycle(api->user,"project.NativeUIProbe",kb::script::ScriptLifecycleEvent::Ready,&Ready)&&api->registerLifecycle(api->user,"project.NativeUIProbe",kb::script::ScriptLifecycleEvent::Tick,&Tick);}
