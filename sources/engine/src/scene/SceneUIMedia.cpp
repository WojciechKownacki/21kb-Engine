#include "scene/SceneUIMedia.hpp"
#include "scene/SceneState.hpp"
#include "scene/SceneAccess.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneUI.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "engine/audio/AudioClipAsset.hpp"
#include "engine/assets/AssetManager.hpp"
#include <chrono>
#include <algorithm>
#include <cstring>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <sapi.h>
#include <wrl/client.h>
#endif
namespace kb::scene {
void InitializeUIMedia(Scene& scene) {
    auto state=std::make_shared<SceneUIMediaState>();
    state->attachWindow=&SceneAccessibilityWindow::Attach;
    state->play=&SceneVideoPlayers::Play;state->seek=&SceneVideoPlayers::Seek;
    state->speak=&SceneAccessibility::SpeakFocused;state->enableReader=&SceneAccessibility::SetScreenReaderEnabled;
    state->waitSpeech=&SceneAccessibility::WaitForSpeech;state->update=&UpdateUIMedia;state->updateReader=&UpdateUIReader;
    SceneAccess::State(scene).uiMedia=std::move(state);
}
SceneUIMediaState& EnsureUIMedia(Scene& scene) {
    auto& state=SceneAccess::State(scene).uiMedia;
    if(!state)InitializeUIMedia(scene);
    return *state;
}
namespace {
UIAccessibilitySpeech Synthesize(std::uint64_t id,std::string text) {
    UIAccessibilitySpeech out;out.id=id;out.text=std::move(text);out.ready=true;
#ifdef _WIN32
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(initialized)){out.error="speech COM initialization failed";return out;}
    {
        using Microsoft::WRL::ComPtr;
        ComPtr<ISpVoice> voice;ComPtr<ISpStream> stream;ComPtr<IStream> memory;
        HRESULT result=CoCreateInstance(CLSID_SpVoice,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&voice));
        if(SUCCEEDED(result))result=CreateStreamOnHGlobal(nullptr,TRUE,&memory);
        if(SUCCEEDED(result))result=CoCreateInstance(CLSID_SpStream,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&stream));
        WAVEFORMATEX format{WAVE_FORMAT_PCM,1,22050,44100,2,16,0};
        if(SUCCEEDED(result))result=stream->SetBaseStream(memory.Get(),SPDFID_WaveFormatEx,&format);
        if(SUCCEEDED(result))result=voice->SetOutput(stream.Get(),FALSE);
        const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,out.text.data(),static_cast<int>(out.text.size()),nullptr,0);
        std::wstring wide(static_cast<std::size_t>(std::max(0,length)),L'\0');
        if(length>0)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,out.text.data(),static_cast<int>(out.text.size()),wide.data(),length);
        if(length<=0)result=E_INVALIDARG;
        if(SUCCEEDED(result))result=voice->Speak(wide.c_str(),SPF_IS_NOT_XML,nullptr);
        STATSTG stat{};if(SUCCEEDED(result))result=memory->Stat(&stat,STATFLAG_NONAME);
        if(SUCCEEDED(result)&&(stat.cbSize.QuadPart==0||stat.cbSize.QuadPart>8U*1024U*1024U))result=E_OUTOFMEMORY;
        if(SUCCEEDED(result)) {
            LARGE_INTEGER zero{};result=memory->Seek(zero,STREAM_SEEK_SET,nullptr);
            std::vector<std::int16_t> pcm(static_cast<std::size_t>(stat.cbSize.QuadPart/2));ULONG read=0;
            if(SUCCEEDED(result))result=memory->Read(pcm.data(),static_cast<ULONG>(pcm.size()*2),&read);
            if(SUCCEEDED(result)&&read==pcm.size()*2) {
                auto samples=std::make_shared<std::vector<float>>();samples->reserve(pcm.size());
                for(const auto value:pcm)samples->push_back(static_cast<float>(value)/32768.0F);
                out.samples=std::move(samples);out.channels=1;out.sampleRate=22050;out.succeeded=true;
            }else result=E_FAIL;
        }
        if(FAILED(result))out.error="Windows speech synthesis failed (HRESULT "+std::to_string(static_cast<std::uint32_t>(result))+")";
    }
    CoUninitialize();
#else
    out.error="built-in speech requires Windows SAPI; no host provider is available";
#endif
    return out;
}
std::shared_ptr<kb::audio::AudioClipAsset> Wave(const UIAccessibilitySpeech& speech) {
    auto clip=std::make_shared<kb::audio::AudioClipAsset>();clip->sourceExtension=".wav";
    const auto count=static_cast<std::uint32_t>(speech.samples->size()*sizeof(float));auto& b=clip->encodedBytes;b.resize(44+count);
    auto u16=[&](std::size_t o,std::uint16_t x){std::memcpy(b.data()+o,&x,2);};auto u32=[&](std::size_t o,std::uint32_t x){std::memcpy(b.data()+o,&x,4);};
    std::memcpy(b.data(),"RIFF",4);u32(4,36+count);std::memcpy(b.data()+8,"WAVEfmt ",8);u32(16,16);u16(20,3);u16(22,static_cast<std::uint16_t>(speech.channels));u32(24,speech.sampleRate);u32(28,speech.sampleRate*speech.channels*4);u16(32,static_cast<std::uint16_t>(speech.channels*4));u16(34,32);std::memcpy(b.data()+36,"data",4);u32(40,count);std::memcpy(b.data()+44,speech.samples->data(),count);return clip;
}
void PollSpeech(Scene& scene,SceneUIMediaState& state) {
    if(!state.speechJob.valid()||state.speechJob.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
    try{state.speech=state.speechJob.get();}catch(const std::exception& e){state.speech.ready=true;state.speech.error=e.what();state.speech.succeeded=false;}
    auto& speech=state.speech;if(state.discardSpeech){speech.succeeded=false;speech.error="speech synthesis was cancelled";speech.samples.reset();state.discardSpeech=false;}if(!speech.succeeded)return;
    // One stable asset slot prevents unbounded decoder-cache growth across focus changes.
    auto& assets=scene.Assets().Manager();const auto id=kb::assets::MakeAssetId("runtime-ui/screen-reader.wav");
    kb::assets::AssetMetadata info;info.id=id;info.type="AudioClip";info.virtualPath="/Runtime/UI/ScreenReader.wav";info.contentHash=speech.id;
    if(!assets.RegisterGeneratedAsset(std::move(info))||!assets.PublishRuntimeAsset(id,Wave(speech))){speech.succeeded=false;speech.error=assets.LastError();return;}
    if(state.speechVoice)static_cast<void>(kb::audio::AudioPlayback::StopVoice(scene,state.speechVoice));
    kb::audio::AudioPlayDesc play;play.clipAssetId=id.value;play.spatial=false;play.spatialBlend=0;
    auto result=kb::audio::AudioPlayback::PlayOneShot(scene,play);speech.voiceId=result.voiceId;state.speechVoice=result.voiceId;
    if(!result.Succeeded()){speech.succeeded=false;speech.error=result.error;}
}
}
std::uint64_t SceneAccessibility::SpeakFocused(Scene& scene,std::string& error) {
    error.clear();auto& state=EnsureUIMedia(scene);if(state.speak!=&SceneAccessibility::SpeakFocused)return state.speak(scene,error);PollSpeech(scene,state);
    if(state.speechJob.valid()){error="speech synthesis is busy";return 0;}
    const auto tree=Read(scene);if(!tree.error.empty()){error=tree.error;return 0;}
    const auto node=std::find_if(tree.nodes.begin(),tree.nodes.end(),[&](const auto& n){return n.entity==tree.focused;});
    if(node==tree.nodes.end()){error="no accessible element has focus";return 0;}
    const auto text=Describe(*node);if(text.empty()||text.size()>2048){error="speech label is empty or exceeds 2048 bytes";return 0;}
    const auto id=state.nextSpeech++;state.discardSpeech=false;state.speech={};state.speech.id=id;
    state.speechJob=std::async(std::launch::async,[id,text]{return Synthesize(id,text);});return id;
}
void SceneAccessibility::SetScreenReaderEnabled(Scene& scene,bool enabled){auto& state=EnsureUIMedia(scene);if(state.enableReader!=&SceneAccessibility::SetScreenReaderEnabled){state.enableReader(scene,enabled);return;}state.readerEnabled=enabled;if(!enabled&&state.speechJob.valid())state.discardSpeech=true;state.announced={};if(!enabled&&state.speechVoice)static_cast<void>(kb::audio::AudioPlayback::StopVoice(scene,state.speechVoice));}
UIAccessibilitySpeech SceneAccessibility::ReadSpeech(const Scene& scene,std::uint64_t id){const auto& state=SceneAccess::State(scene).uiMedia;return state&&state->speech.id==id?state->speech:UIAccessibilitySpeech{};}
bool SceneAccessibility::WaitForSpeech(Scene& scene,std::uint64_t id,std::uint32_t timeout){auto& state=EnsureUIMedia(scene);if(state.waitSpeech!=&SceneAccessibility::WaitForSpeech)return state.waitSpeech(scene,id,timeout);if(state.speech.id!=id)return false;if(state.speechJob.valid())state.speechJob.wait_for(std::chrono::milliseconds(std::min(timeout,30000U)));PollSpeech(scene,state);return state.speech.id==id&&state.speech.ready&&state.speech.succeeded;}
void ShutdownUIMedia(Scene& scene) {
    auto& state=SceneAccess::State(scene).uiMedia;if(!state)return;
    SceneAccessibilityWindow::Detach(scene);
    if(state->speechVoice)static_cast<void>(kb::audio::AudioPlayback::StopVoice(scene,state->speechVoice));
    for(auto& p:state->videos){static_cast<void>(kb::audio::AudioPlayback::StopVoice(scene,p.status.voiceId));static_cast<void>(scene.Assets().Manager().ReleaseGeneratedAsset(kb::assets::AssetId{p.imageId}));static_cast<void>(scene.Assets().Manager().ReleaseGeneratedAsset(kb::assets::AssetId{p.clipId}));}
    static_cast<void>(scene.Assets().Manager().ReleaseGeneratedAsset(kb::assets::MakeAssetId("runtime-ui/screen-reader.wav")));
    // Joining the single bounded job and dropping decoder payloads precede module unload.
    state.reset();static_cast<void>(scene.Assets().Manager().PruneUnreferenced());
}
void UpdateUIReader(Scene& scene){const auto state=SceneAccess::State(scene).uiMedia;if(!state)return;if(state->updateReader!=&UpdateUIReader){state->updateReader(scene);return;}PublishUIAccessibilityWindow(scene);PollSpeech(scene,*state);if(state->readerEnabled&&!state->speechJob.valid()&&state->announced!=scene.UI().Focused()){std::string error;if(SceneAccessibility::SpeakFocused(scene,error))state->announced=scene.UI().Focused();}}
}
