#include "engine/scene/SceneVideoPlayers.hpp"
#include "scene/SceneUIMedia.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/ImportedAsset.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "engine/audio/AudioClipAsset.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#elif defined(__ANDROID__)
#include "scene/AndroidVideoSource.hpp"
#include <memory>
#endif
namespace kb::scene {
class VideoDecoder {
public:
    double duration=0,videoTime=0;
    std::uint32_t width=0,height=0;
    std::uint64_t revision=0,outputClockFrame=0;
    std::uint32_t clockRate=0;
    std::shared_ptr<kb::audio::AudioClipAsset> audio;
    std::shared_ptr<DynamicImageAsset> frame;
    bool Open(std::span<const std::byte>,std::string&);
    bool Advance(double,std::string&);
    bool Seek(double,std::string&);
#ifdef _WIN32
    ~VideoDecoder(){pending.Reset();video.Reset();audioReader.Reset();if(started)shutdown();if(readLib)FreeLibrary(readLib);if(platLib)FreeLibrary(platLib);if(comOwned)CoUninitialize();}
private:
    template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<IMFSourceReader> video,audioReader;Ptr<IMFSample> pending;
    double pendingTime=0;
    LONG stride=0;
    bool comOwned=false,started=false,eof=false;
    HMODULE platLib=nullptr,readLib=nullptr;
    decltype(&MFStartup) startup=nullptr;
    decltype(&MFShutdown) shutdown=nullptr;
    decltype(&MFCreateMFByteStreamOnStream) byteStream=nullptr;
    decltype(&MFCreateAttributes) attributes=nullptr;
    decltype(&MFCreateMediaType) mediaType=nullptr;
    decltype(&MFCreateSourceReaderFromByteStream) reader=nullptr;
    HRESULT Reader(std::span<const std::byte> bytes,Ptr<IMFSourceReader>& out) {
        Ptr<IStream> stream;HRESULT hr=CreateStreamOnHGlobal(nullptr,TRUE,&stream);ULONG written=0;
        if(SUCCEEDED(hr))hr=stream->Write(bytes.data(),static_cast<ULONG>(bytes.size()),&written);
        LARGE_INTEGER zero{};if(SUCCEEDED(hr))hr=stream->Seek(zero,STREAM_SEEK_SET,nullptr);
        Ptr<IMFByteStream> mfstream;if(SUCCEEDED(hr))hr=byteStream(stream.Get(),&mfstream);
        Ptr<IMFAttributes> attr;if(SUCCEEDED(hr))hr=attributes(&attr,2);
        if(SUCCEEDED(hr))hr=attr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE);
        if(SUCCEEDED(hr))hr=reader(mfstream.Get(),attr.Get(),&out);
        return hr;
    }
    bool Fail(HRESULT hr,std::string& error) {error="Windows video decoding failed (HRESULT "+std::to_string(static_cast<std::uint32_t>(hr))+")";return false;}
    bool Publish(IMFSample* sample,double pts,std::string& error) {
        Ptr<IMFMediaBuffer> buffer;HRESULT hr=sample->ConvertToContiguousBuffer(&buffer);if(FAILED(hr))return Fail(hr,error);
        Ptr<IMF2DBuffer> two;BYTE* bytes=nullptr;LONG pitch=stride;DWORD current=0,max=0;
        const bool locked2=SUCCEEDED(buffer.As(&two))&&SUCCEEDED(two->Lock2D(&bytes,&pitch));
        if(!locked2){hr=buffer->Lock(&bytes,&max,&current);if(FAILED(hr))return Fail(hr,error);if(current<static_cast<std::uint64_t>(std::abs(pitch))*height){buffer->Unlock();error="video frame buffer is truncated";return false;}if(pitch<0)bytes+=static_cast<std::size_t>(height-1)*static_cast<std::size_t>(-pitch);}
        auto image=std::make_shared<DynamicImageAsset>();image->width=width;image->height=height;image->revision=++revision;image->rgba.resize(static_cast<std::size_t>(width)*height*4);
        for(std::uint32_t y=0;y<height;++y){const BYTE* row=bytes+static_cast<std::ptrdiff_t>(y)*pitch;auto* dst=image->rgba.data()+static_cast<std::size_t>(y)*width*4;for(std::uint32_t x=0;x<width;++x){dst[x*4]=row[x*4+2];dst[x*4+1]=row[x*4+1];dst[x*4+2]=row[x*4];dst[x*4+3]=255;}}
        if(locked2)two->Unlock2D();else buffer->Unlock();frame=std::move(image);videoTime=pts;return true;
    }
#elif defined(__ANDROID__)
private:
    std::unique_ptr<AndroidVideoSource> source;
    DecodedPicture pending,staged;
    bool hasPending=false,hasStaged=false,eof=false;
    bool AdvanceTo(double,std::size_t,std::string&);
    bool Publish(std::string&);
#endif
};
#ifdef _WIN32
bool VideoDecoder::Open(std::span<const std::byte> bytes,std::string& error) {
    const auto init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(init)&&init!=RPC_E_CHANGED_MODE)return Fail(init,error);comOwned=SUCCEEDED(init);
    platLib=LoadLibraryExW(L"mfplat.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);readLib=LoadLibraryExW(L"mfreadwrite.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!platLib||!readLib){error="Windows Media Foundation is unavailable; install the Media Feature Pack";return false;}
#define KB_MF_FN(member,library,name) member=reinterpret_cast<decltype(member)>(GetProcAddress(library,name));if(!member){error="Media Foundation entry point is unavailable";return false;}
    KB_MF_FN(startup,platLib,"MFStartup") KB_MF_FN(shutdown,platLib,"MFShutdown") KB_MF_FN(byteStream,platLib,"MFCreateMFByteStreamOnStream") KB_MF_FN(attributes,platLib,"MFCreateAttributes") KB_MF_FN(mediaType,platLib,"MFCreateMediaType") KB_MF_FN(reader,readLib,"MFCreateSourceReaderFromByteStream")
#undef KB_MF_FN
    HRESULT hr=startup(MF_VERSION,MFSTARTUP_FULL);if(FAILED(hr))return Fail(hr,error);started=true;
    hr=Reader(bytes,video);if(FAILED(hr))return Fail(hr,error);hr=Reader(bytes,audioReader);if(FAILED(hr))return Fail(hr,error);
    PROPVARIANT length{};hr=video->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE),MF_PD_DURATION,&length);if(SUCCEEDED(hr)&&length.vt==VT_UI8)duration=static_cast<double>(length.uhVal.QuadPart)/10000000.0;PropVariantClear(&length);
    if(duration<=0||duration>60){error="UI video duration must be positive and at most 60 seconds";return false;}
    Ptr<IMFMediaType> native;hr=video->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,&native);if(FAILED(hr))return Fail(hr,error);
    UINT32 rotation=0,transfer=0,primaries=0;native->GetUINT32(MF_MT_VIDEO_ROTATION,&rotation);native->GetUINT32(MF_MT_TRANSFER_FUNCTION,&transfer);native->GetUINT32(MF_MT_VIDEO_PRIMARIES,&primaries);
    if(rotation!=0||transfer==15||transfer==16||primaries==9){error="UI video supports unrotated SDR content; rotate or tone-map the source before importing";return false;}
    Ptr<IMFMediaType> type;hr=mediaType(&type);if(SUCCEEDED(hr))hr=type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);if(SUCCEEDED(hr))hr=type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);
    if(SUCCEEDED(hr))hr=video->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS),FALSE);if(SUCCEEDED(hr))hr=video->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),TRUE);if(SUCCEEDED(hr))hr=video->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),nullptr,type.Get());if(FAILED(hr))return Fail(hr,error);
    hr=video->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),&type);UINT64 size=0;if(SUCCEEDED(hr))hr=type->GetUINT64(MF_MT_FRAME_SIZE,&size);if(FAILED(hr))return Fail(hr,error);width=static_cast<UINT32>(size>>32);height=static_cast<UINT32>(size);
    if(width==0||height==0||width>1920||height>1080){error="UI video exceeds the 1920x1080 frame budget";return false;}
    UINT32 rawStride=0;if(SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE,&rawStride)))stride=static_cast<LONG>(rawStride);else stride=static_cast<LONG>(width*4);
    if(std::abs(static_cast<std::int64_t>(stride))<width*4){error="invalid video row stride";return false;}
    type.Reset();hr=mediaType(&type);if(SUCCEEDED(hr))hr=type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);if(SUCCEEDED(hr))hr=type->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_Float);if(SUCCEEDED(hr))hr=type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2);if(SUCCEEDED(hr))hr=type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,48000);if(SUCCEEDED(hr))hr=type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,32);if(SUCCEEDED(hr))hr=type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,8);if(SUCCEEDED(hr))hr=type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,384000);
    if(SUCCEEDED(hr))hr=audioReader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS),FALSE);if(SUCCEEDED(hr))hr=audioReader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),TRUE);if(SUCCEEDED(hr))hr=audioReader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),nullptr,type.Get());if(FAILED(hr)){error="UI video requires a decodable audio track: "+std::to_string(static_cast<std::uint32_t>(hr));return false;}
    const auto frames=static_cast<std::size_t>(std::ceil(duration*48000));std::vector<std::uint8_t> pcm(frames*8,0);bool found=false,audioEnd=false;
    for(std::size_t budget=0;budget<10000;++budget){DWORD flags=0;LONGLONG pts=0;Ptr<IMFSample> sample;hr=audioReader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),0,nullptr,&flags,&pts,&sample);if(FAILED(hr))return Fail(hr,error);if(flags&MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED){error="audio format changes during video are unsupported";return false;}
        if(sample){Ptr<IMFMediaBuffer> buffer;hr=sample->ConvertToContiguousBuffer(&buffer);if(FAILED(hr))return Fail(hr,error);BYTE* data=nullptr;DWORD sizeBytes=0;hr=buffer->Lock(&data,nullptr,&sizeBytes);if(FAILED(hr))return Fail(hr,error);const auto first=static_cast<std::int64_t>(std::llround(static_cast<double>(pts)*.0048));const auto skip=first<0?static_cast<std::size_t>(-first):0;const auto dest=first<0?0:static_cast<std::size_t>(first);const auto sourceFrames=sizeBytes/8;if(skip<sourceFrames&&dest<frames){const auto count=std::min<std::size_t>(sourceFrames-skip,frames-dest);std::memcpy(pcm.data()+dest*8,data+skip*8,count*8);found=true;}buffer->Unlock();}
        if(flags&MF_SOURCE_READERF_ENDOFSTREAM){audioEnd=true;break;}}
    if(!found||!audioEnd){error="video audio is empty or exceeds the decode budget";return false;}
    audio=std::make_shared<kb::audio::AudioClipAsset>();audio->sourceExtension=".wav";auto& b=audio->encodedBytes;b.resize(44+pcm.size());auto u16=[&](std::size_t o,std::uint16_t v){std::memcpy(b.data()+o,&v,2);};auto u32=[&](std::size_t o,std::uint32_t v){std::memcpy(b.data()+o,&v,4);};std::memcpy(b.data(),"RIFF",4);u32(4,static_cast<std::uint32_t>(36+pcm.size()));std::memcpy(b.data()+8,"WAVEfmt ",8);u32(16,16);u16(20,3);u16(22,2);u32(24,48000);u32(28,384000);u16(32,8);u16(34,32);std::memcpy(b.data()+36,"data",4);u32(40,static_cast<std::uint32_t>(pcm.size()));std::memcpy(b.data()+44,pcm.data(),pcm.size());audioReader.Reset();
    return Advance(0,error)&&frame!=nullptr;
}
bool VideoDecoder::Seek(double seconds,std::string& error){PROPVARIANT target{};target.vt=VT_I8;target.hVal.QuadPart=static_cast<LONGLONG>(seconds*10000000.0);const auto hr=video->SetCurrentPosition(GUID_NULL,target);if(FAILED(hr))return Fail(hr,error);pending.Reset();eof=false;return Advance(seconds,error);}
bool VideoDecoder::Advance(double seconds,std::string& error){
    for(std::size_t budget=0;budget<64;++budget){
        if(pending){if(pendingTime>seconds&&frame)return true;if(!Publish(pending.Get(),pendingTime,error))return false;pending.Reset();}
        if(eof)return true;
        DWORD flags=0;LONGLONG pts=0;const auto hr=video->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,nullptr,&flags,&pts,&pending);if(FAILED(hr))return Fail(hr,error);
        if(flags&MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED){error="video format changes during playback are unsupported";return false;}
        if(flags&MF_SOURCE_READERF_ENDOFSTREAM)eof=true;pendingTime=static_cast<double>(pts)/10000000.0;
    }
    error="UI video exceeds the 64 decoded frames per update budget";return false;
}
#elif defined(__ANDROID__)
bool VideoDecoder::Open(std::span<const std::byte> bytes,std::string& error) {
    source=std::make_unique<AndroidVideoSource>();if(!source->Open(bytes,error))return false;
    duration=source->duration;width=source->width;height=source->height;
    const auto pcmBytes=source->audio.size()*sizeof(float);
    audio=std::make_shared<kb::audio::AudioClipAsset>();audio->sourceExtension=".wav";auto& b=audio->encodedBytes;b.resize(44+pcmBytes);auto u16=[&](std::size_t o,std::uint16_t v){std::memcpy(b.data()+o,&v,2);};auto u32=[&](std::size_t o,std::uint32_t v){std::memcpy(b.data()+o,&v,4);};std::memcpy(b.data(),"RIFF",4);u32(4,static_cast<std::uint32_t>(36+pcmBytes));std::memcpy(b.data()+8,"WAVEfmt ",8);u32(16,16);u16(20,3);u16(22,2);u32(24,48000);u32(28,384000);u16(32,8);u16(34,32);std::memcpy(b.data()+36,"data",4);u32(40,static_cast<std::uint32_t>(pcmBytes));std::memcpy(b.data()+44,source->audio.data(),pcmBytes);
    source->audio=std::vector<float>{};
    if(!Advance(0,error))return false;if(!frame){error="UI video has no decodable frame";return false;}return true;
}
// After a seek the decoder restarts at a sync sample, so catching up to the target may need a whole group of pictures.
bool VideoDecoder::Seek(double seconds,std::string& error){if(!source->Seek(seconds,error))return false;hasPending=hasStaged=eof=false;return AdvanceTo(seconds,1024,error);}
bool VideoDecoder::Advance(double seconds,std::string& error){return AdvanceTo(seconds,64,error);}
// Only the newest picture at or before `seconds` is color converted; earlier ones are dropped as raw buffers.
bool VideoDecoder::AdvanceTo(double seconds,std::size_t limit,std::string& error){
    for(std::size_t budget=0;budget<limit;++budget){
        if(hasPending){if(pending.time>seconds&&(frame||hasStaged))return Publish(error);std::swap(staged,pending);hasStaged=true;hasPending=false;}
        if(eof)return Publish(error);
        bool end=false;if(!source->NextPicture(pending,end,error))return false;
        if(end)eof=true;else hasPending=true;
    }
    error="UI video exceeds the "+std::to_string(limit)+" decoded frames per update budget";return false;
}
bool VideoDecoder::Publish(std::string& error){
    if(!hasStaged)return true;hasStaged=false;
    if(!frame){width=staged.layout.width;height=staged.layout.height;if(width>1920||height>1080){error="UI video exceeds the 1920x1080 frame budget";return false;}}
    else if(staged.layout.width!=width||staged.layout.height!=height){error="video format changes during playback are unsupported";return false;}
    auto image=std::make_shared<DynamicImageAsset>();image->width=width;image->height=height;image->revision=revision+1;image->rgba.resize(static_cast<std::size_t>(width)*height*4);
    if(!ConvertYuvToRgba(staged.layout,staged.bytes,image->rgba.data())){error="video frame buffer is truncated";return false;}
    revision=image->revision;frame=std::move(image);videoTime=staged.time;return true;
}
#else
bool VideoDecoder::Open(std::span<const std::byte>,std::string& error){error="UI video decoding requires Windows or Android";return false;}
bool VideoDecoder::Seek(double,std::string& error){error="UI video decoding requires Windows or Android";return false;}
bool VideoDecoder::Advance(double,std::string& error){error="UI video decoding requires Windows or Android";return false;}
#endif
namespace {
UIVideoRecord* Find(SceneUIMediaState& state,SceneEntity owner){const auto it=std::find_if(state.videos.begin(),state.videos.end(),[&](const auto& p){return p.owner==owner;});return it==state.videos.end()?nullptr:&*it;}
bool Publish(Scene& scene,UIVideoRecord& record){if(!record.decoder->frame)return false;auto& assets=scene.Assets().Manager();if(!assets.PublishRuntimeAsset(kb::assets::AssetId{record.imageId},record.decoder->frame)){record.status.error=assets.LastError();return false;}record.status.frameRevision=record.decoder->revision;record.status.videoSeconds=record.decoder->videoTime;return true;}
}
bool SceneVideoPlayers::Play(Scene& scene,SceneEntity owner,std::string_view path,bool loop,std::string& error){
    error.clear();const auto* image=scene.Components().UI().TryGet<UIRawImage>(owner);if(!scene.Entities().IsAlive(owner)||!scene.Entities().IsActive(owner)||!image){error="UI video owner requires UIRawImage";return false;}
    auto& state=EnsureUIMedia(scene);if(state.play!=&SceneVideoPlayers::Play)return state.play(scene,owner,path,loop,error);if(state.videos.size()>=4&&!Find(state,owner)){error="UI video player budget is four";return false;}
    auto& assets=scene.Assets().Manager();const auto* info=assets.Registry().FindByPath(kb::assets::Utf8Path(path));if(!info){error="video asset was not found";return false;}const auto imported=assets.Load<kb::assets::ImportedAsset>(info->id);if(!imported.IsLoaded()||imported->category!=kb::assets::AssetImportCategory::Video){error="UI video requires an imported Video asset";return false;}
    if(imported->payload.empty()||imported->payload.size()>64U*1024U*1024U){error="UI video source exceeds 64 MiB or is empty";return false;}
    auto decoder=std::make_shared<VideoDecoder>();if(!decoder->Open(imported->payload,error))return false;
    const auto outputClock=kb::audio::AudioPlayback::DspReport(scene);
    if(outputClock.sampleRate==0){error="UI video requires an audio backend with a rendered-frame clock";return false;}
    decoder->outputClockFrame=outputClock.renderedFrames;decoder->clockRate=outputClock.sampleRate;
    const auto clipId=kb::assets::MakeAssetId("runtime-ui/video-audio/"+std::to_string(owner.Id()));const auto imageId=kb::assets::MakeAssetId("runtime-ui/video-image/"+std::to_string(owner.Id()));
    kb::assets::AssetMetadata clipInfo;clipInfo.id=clipId;clipInfo.type="AudioClip";clipInfo.virtualPath="/Runtime/UI/VideoAudio"+std::to_string(owner.Id())+".wav";clipInfo.contentHash=info->contentHash;
    kb::assets::AssetMetadata imageInfo;imageInfo.id=imageId;imageInfo.type="DynamicImage";imageInfo.virtualPath="/Runtime/UI/VideoImage"+std::to_string(owner.Id());
    if(!assets.RegisterGeneratedAsset(std::move(clipInfo))||!assets.PublishRuntimeAsset(clipId,decoder->audio)||!assets.RegisterGeneratedAsset(std::move(imageInfo))||!assets.PublishRuntimeAsset(imageId,decoder->frame)){error=assets.LastError();return false;}
    kb::audio::AudioPlayDesc desc;desc.clipAssetId=clipId.value;desc.spatial=false;desc.spatialBlend=0;desc.loop=loop;desc.ownerEntityId=owner.Id();auto played=kb::audio::AudioPlayback::PlayOneShot(scene,desc);if(!played.Succeeded()){error=played.error;return false;}
    UIRawImage previous=*image;if(auto* old=Find(state,owner)){previous=old->previousImage;static_cast<void>(kb::audio::AudioPlayback::StopVoice(scene,old->status.voiceId));std::erase_if(state.videos,[&](const auto& p){return p.owner==owner;});}
    UIVideoRecord record;record.owner=owner;record.clipId=clipId.value;record.imageId=imageId.value;record.previousImage=previous;record.loop=loop;record.decoder=std::move(decoder);record.status.available=true;record.status.playing=true;record.status.voiceId=played.voiceId;record.status.durationSeconds=record.decoder->duration;record.status.width=record.decoder->width;record.status.height=record.decoder->height;record.status.frameRevision=record.decoder->revision;record.status.videoSeconds=record.decoder->videoTime;
    auto current=previous;current.imageAssetId=imageId.value;scene.Components().UI().Set(owner,current);state.videos.push_back(std::move(record));return true;
}
UIVideoStatus SceneVideoPlayers::Read(const Scene& scene,SceneEntity owner){const auto& state=SceneAccess::State(scene).uiMedia;if(!state)return {};const auto* p=Find(*state,owner);return p?p->status:UIVideoStatus{};}
bool SceneVideoPlayers::Pause(Scene& scene,SceneEntity owner){auto& state=EnsureUIMedia(scene);auto* p=Find(state,owner);if(!p||!p->status.playing||!kb::audio::AudioPlayback::PauseVoice(scene,p->status.voiceId))return false;p->status.paused=true;p->status.playing=false;const auto clock=kb::audio::AudioPlayback::VoicePlaybackSeconds(scene,p->status.voiceId);if(clock<0){p->status.error="video audio cursor became unavailable while pausing";return false;}p->status.audioSeconds=clock;std::string error;if(!p->decoder->Advance(clock,error)){p->status.error=error;return false;}return Publish(scene,*p);}
bool SceneVideoPlayers::Resume(Scene& scene,SceneEntity owner){auto& state=EnsureUIMedia(scene);auto* p=Find(state,owner);if(!p||!p->status.paused||!kb::audio::AudioPlayback::ResumeVoice(scene,p->status.voiceId))return false;p->status.paused=false;p->status.playing=true;const auto outputClock=kb::audio::AudioPlayback::DspReport(scene);p->decoder->outputClockFrame=outputClock.renderedFrames;p->decoder->clockRate=outputClock.sampleRate;return true;}
bool SceneVideoPlayers::Seek(Scene& scene,SceneEntity owner,double seconds,std::string& error){error.clear();auto& state=EnsureUIMedia(scene);if(state.seek!=&SceneVideoPlayers::Seek)return state.seek(scene,owner,seconds,error);auto* p=Find(state,owner);if(!p||!std::isfinite(seconds)||seconds<0||seconds>=p->status.durationSeconds){error="video seek is outside the active clip";return false;}
    const bool wasPlaying=p->status.playing;if(wasPlaying&&!Pause(scene,owner)){error="video audio could not pause for seek";return false;}
    if(!p->decoder->Seek(seconds,error)){p->status.error=error;return false;}
    if(!kb::audio::AudioPlayback::SeekVoice(scene,p->status.voiceId,static_cast<float>(seconds))){error="video audio seek was rejected";p->status.error=error;return false;}
    const auto outputClock=kb::audio::AudioPlayback::DspReport(scene);p->decoder->outputClockFrame=outputClock.renderedFrames;p->decoder->clockRate=outputClock.sampleRate;
    p->status.audioSeconds=seconds;p->status.ended=false;if(!Publish(scene,*p)){error=p->status.error;return false;}return !wasPlaying||Resume(scene,owner);
}
void SceneVideoPlayers::Stop(Scene& scene,SceneEntity owner){const auto state=SceneAccess::State(scene).uiMedia;if(!state)return;auto* p=Find(*state,owner);if(!p)return;static_cast<void>(kb::audio::AudioPlayback::StopVoice(scene,p->status.voiceId));if(scene.Entities().IsAlive(owner))scene.Components().UI().Set(owner,p->previousImage);static_cast<void>(scene.Assets().Manager().ReleaseGeneratedAsset(kb::assets::AssetId{p->imageId}));static_cast<void>(scene.Assets().Manager().ReleaseGeneratedAsset(kb::assets::AssetId{p->clipId}));std::erase_if(state->videos,[&](const auto& v){return v.owner==owner;});}
void UpdateUIMedia(Scene& scene){const auto state=SceneAccess::State(scene).uiMedia;if(!state)return;if(state->update!=&UpdateUIMedia){state->update(scene);return;}
    const auto outputClock=kb::audio::AudioPlayback::DspReport(scene);
    for(auto& p:state->videos){if(!scene.Entities().IsAlive(p.owner)){static_cast<void>(kb::audio::AudioPlayback::StopVoice(scene,p.status.voiceId));static_cast<void>(scene.Assets().Manager().ReleaseGeneratedAsset(kb::assets::AssetId{p.imageId}));static_cast<void>(scene.Assets().Manager().ReleaseGeneratedAsset(kb::assets::AssetId{p.clipId}));continue;}if(!p.status.playing)continue;
        const auto clock=kb::audio::AudioPlayback::VoicePlaybackSeconds(scene,p.status.voiceId);if(clock<0){
            p.status.playing=false;const bool sameClock=outputClock.sampleRate!=0&&outputClock.sampleRate==p.decoder->clockRate&&outputClock.renderedFrames>=p.decoder->outputClockFrame;
            const double elapsed=sameClock?static_cast<double>(outputClock.renderedFrames-p.decoder->outputClockFrame)/outputClock.sampleRate:0;
            p.status.ended=!p.loop&&sameClock&&p.status.audioSeconds+elapsed>=p.status.durationSeconds-1.1/outputClock.sampleRate;
            if(p.status.ended){p.status.audioSeconds=p.status.durationSeconds;std::string error;if(p.decoder->Advance(p.status.durationSeconds,error))static_cast<void>(Publish(scene,p));else p.status.error=error;}
            else p.status.error="video audio voice stopped or its output clock reset before the clip ended";continue;
        }
        p.decoder->outputClockFrame=outputClock.renderedFrames;p.decoder->clockRate=outputClock.sampleRate;
        std::string error;if(clock+.001<p.status.audioSeconds&&p.loop){if(!p.decoder->Seek(0,error))p.status.error=error;}p.status.audioSeconds=clock;
        if(p.status.error.empty()&&!p.decoder->Advance(clock,error))p.status.error=error;
        if(p.status.error.empty()){static_cast<void>(Publish(scene,p));if(!p.loop&&clock>=p.status.durationSeconds-0.00001){p.status.ended=true;p.status.playing=false;}}else{p.status.playing=false;static_cast<void>(kb::audio::AudioPlayback::PauseVoice(scene,p.status.voiceId));}}
    std::erase_if(state->videos,[&](const auto& p){return !scene.Entities().IsAlive(p.owner);});
}
}
