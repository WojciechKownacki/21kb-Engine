#include "runtime/MiniaudioSpatialStream.hpp"
#include "runtime/MiniaudioSound.hpp"
#include "assets/ResolvedAudioClip.hpp"
#include "assets/MiniaudioEncodingFormat.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#if KB_AUDIO_HAS_STEAMAUDIO
#include <phonon.h>
#endif

namespace kb::audio_miniaudio {
#if KB_AUDIO_HAS_STEAMAUDIO
namespace {
constexpr ma_uint32 kFrames=512;
extern "C" float kb_miniaudio_distance_attenuation(ma_attenuation_model,float,float,float,float);
struct SpatialContext {
    IPLContext context=nullptr;
    IPLHRTF hrtf=nullptr;
    IPLAudioSettings audio{};
    ~SpatialContext(){if(hrtf) iplHRTFRelease(&hrtf);if(context) iplContextRelease(&context);}
    static std::shared_ptr<SpatialContext> Acquire(ma_uint32 rate) {
        static std::mutex mutex;static std::map<ma_uint32,std::weak_ptr<SpatialContext>> cache;
        const std::lock_guard guard{mutex};if(auto existing=cache[rate].lock()) return existing;
        auto result=std::make_shared<SpatialContext>();IPLContextSettings settings{};settings.version=STEAMAUDIO_VERSION;settings.simdLevel=IPL_SIMDLEVEL_SSE2;
        if(iplContextCreate(&settings,&result->context)!=IPL_STATUS_SUCCESS) return {};
        result->audio={static_cast<IPLint32>(rate),static_cast<IPLint32>(kFrames)};IPLHRTFSettings hrtf{};hrtf.type=IPL_HRTFTYPE_DEFAULT;hrtf.volume=1;hrtf.normType=IPL_HRTFNORMTYPE_NONE;
        if(iplHRTFCreate(result->context,&result->audio,&hrtf,&result->hrtf)!=IPL_STATUS_SUCCESS) return {};
        cache[rate]=result;return result;
    }
};
kb::scene::Vec3 Vec(ma_vec3f p){return {p.x,p.y,p.z};}
IPLVector3 Reflected(kb::scene::Vec3 p){return {p.x,p.y,-p.z};}
ma_attenuation_model Attenuation(kb::audio::AudioAttenuationModel model) {
    switch(model){case kb::audio::AudioAttenuationModel::None:return ma_attenuation_model_none;case kb::audio::AudioAttenuationModel::Linear:return ma_attenuation_model_linear;case kb::audio::AudioAttenuationModel::Exponential:return ma_attenuation_model_exponential;case kb::audio::AudioAttenuationModel::Inverse:return ma_attenuation_model_inverse;}
    return ma_attenuation_model_none;
}
}
struct MiniaudioSpatialStream::Impl {
    ma_data_source_base base{};
    ma_decoder decoder{};
    bool decoderReady=false,baseReady=false,draining=false,orientationPrimed=false;
    ma_engine* engine=nullptr;
    std::shared_ptr<const void> payload;
    std::shared_ptr<SpatialContext> shared;
    IPLBinauralEffect binaural=nullptr;
    IPLAmbisonicsDecodeEffect ambisonics=nullptr;
    kb::audio::SpatialAudioMode mode=kb::audio::SpatialAudioMode::Binaural;
    ma_uint32 channels=1,rate=0,cursor=0,available=0;
    ma_uint64 length=0,consumed=0;
    std::atomic<bool> looping{false};
    std::atomic<float> px{0},py{0},pz{0},blend{1},minimum{1},maximum{500},rolloff{1};
    std::atomic<ma_attenuation_model> attenuation{ma_attenuation_model_inverse};
    std::array<float,kFrames*4> interleaved{};
    IPLAudioBuffer inputBuffer{},nativeBuffer{},outputBuffer{};
    std::array<float,kFrames*2> stereo{};
    ~Impl(){if(shared){if(inputBuffer.data)iplAudioBufferFree(shared->context,&inputBuffer);if(nativeBuffer.data)iplAudioBufferFree(shared->context,&nativeBuffer);if(outputBuffer.data)iplAudioBufferFree(shared->context,&outputBuffer);}if(baseReady) ma_data_source_uninit(&base);if(binaural) iplBinauralEffectRelease(&binaural);if(ambisonics) iplAmbisonicsDecodeEffectRelease(&ambisonics);if(decoderReady) ma_decoder_uninit(&decoder);}
    void ResetEffect() {
        if(binaural) iplBinauralEffectReset(binaural);if(ambisonics) iplAmbisonicsDecodeEffectReset(ambisonics);cursor=available=0;draining=false;orientationPrimed=false;
    }
    ma_result Fill() {
        if(!draining) {
            ma_uint64 frames=0;bool rewoundWithoutRead=false;
            while(frames<kFrames) {
                ma_uint64 read=0;const auto status=ma_decoder_read_pcm_frames(&decoder,interleaved.data()+frames*channels,kFrames-frames,&read);
                frames+=read;if(read>0) rewoundWithoutRead=false;if(status!=MA_SUCCESS&&status!=MA_AT_END) return status;
                if(read==0){if(looping.load(std::memory_order_relaxed)&&length>0){if(rewoundWithoutRead)return MA_ERROR;rewoundWithoutRead=true;const auto seek=ma_decoder_seek_to_pcm_frame(&decoder,0);if(seek!=MA_SUCCESS) return seek;continue;}draining=true;break;}
            }
            if(frames==0) return Fill();
            std::fill(interleaved.begin()+static_cast<std::ptrdiff_t>(frames*channels),interleaved.end(),0.0F);
            for(ma_uint32 channel=0;channel<channels;++channel) for(ma_uint32 frame=0;frame<kFrames;++frame) inputBuffer.data[channel][frame]=interleaved[frame*channels+channel];
            const auto listener=Vec(ma_engine_listener_get_position(engine,0));
            const auto ahead=kb::math::Normalize(Vec(ma_engine_listener_get_direction(engine,0)));
            const auto right=kb::math::Normalize(kb::math::Cross(Vec(ma_engine_listener_get_world_up(engine,0)),ahead));
            const auto up=kb::math::Cross(ahead,right);
            if(binaural) {
                auto relative=kb::scene::Vec3{px.load(std::memory_order_relaxed),py.load(std::memory_order_relaxed),pz.load(std::memory_order_relaxed)}-listener;
                if(kb::math::Dot(relative,relative)<1.0e-12F) relative=ahead;
                relative=kb::math::Normalize(relative);IPLBinauralEffectParams params{};
                params.direction={kb::math::Dot(relative,right),kb::math::Dot(relative,up),-kb::math::Dot(relative,ahead)};
                params.interpolation=IPL_HRTFINTERPOLATION_BILINEAR;params.spatialBlend=blend.load(std::memory_order_relaxed);params.hrtf=shared->hrtf;
                iplBinauralEffectApply(binaural,&params,&inputBuffer,&outputBuffer);
            } else {
                IPLAmbisonicsDecodeEffectParams params{};params.order=1;params.hrtf=shared->hrtf;params.binaural=IPL_TRUE;
                params.orientation={Reflected(right),Reflected(up),Reflected(ahead),Reflected(listener)};
                // Seed the initial orientation before audible input. Later changes
                // retain the SDK's block interpolation, including after a loop.
                if(!orientationPrimed) {
                    for(int channel=0;channel<4;++channel) std::fill_n(nativeBuffer.data[channel],kFrames,0.0F);
                    iplAmbisonicsDecodeEffectApply(ambisonics,&params,&nativeBuffer,&outputBuffer);orientationPrimed=true;
                }
                iplAudioBufferConvertAmbisonics(shared->context,IPL_AMBISONICSTYPE_SN3D,IPL_AMBISONICSTYPE_N3D,&inputBuffer,&nativeBuffer);
                iplAmbisonicsDecodeEffectApply(ambisonics,&params,&nativeBuffer,&outputBuffer);
            }
        } else {
            const auto tail=binaural?iplBinauralEffectGetTailSize(binaural):iplAmbisonicsDecodeEffectGetTailSize(ambisonics);
            if(tail<=0) return MA_AT_END;
            if(binaural) iplBinauralEffectGetTail(binaural,&outputBuffer);else iplAmbisonicsDecodeEffectGetTail(ambisonics,&outputBuffer);
        }
        float gain=1;
        if(binaural) {
            const auto listener=Vec(ma_engine_listener_get_position(engine,0));const auto delta=kb::scene::Vec3{px.load(std::memory_order_relaxed),py.load(std::memory_order_relaxed),pz.load(std::memory_order_relaxed)}-listener;
            gain=kb_miniaudio_distance_attenuation(attenuation.load(std::memory_order_relaxed),std::sqrt(kb::math::Dot(delta,delta)),minimum.load(std::memory_order_relaxed),maximum.load(std::memory_order_relaxed),rolloff.load(std::memory_order_relaxed));
        }
        for(ma_uint32 frame=0;frame<kFrames;++frame) for(ma_uint32 channel=0;channel<2;++channel) stereo[frame*2+channel]=outputBuffer.data[channel][frame]*gain;
        cursor=0;available=kFrames;return MA_SUCCESS;
    }
    static Impl& Self(ma_data_source* p){return *reinterpret_cast<Impl*>(p);}
    static ma_result Read(ma_data_source* p,void* destination,ma_uint64 count,ma_uint64* framesRead) {
        auto& self=Self(p);ma_uint64 done=0;ma_result status=MA_SUCCESS;
        while(done<count) {
            if(self.cursor==self.available){status=self.Fill();if(status!=MA_SUCCESS) break;}
            const auto n=std::min<ma_uint64>(count-done,self.available-self.cursor);
            if(destination) std::copy_n(self.stereo.data()+self.cursor*2,static_cast<std::size_t>(n*2),static_cast<float*>(destination)+done*2);
            self.cursor+=static_cast<ma_uint32>(n);done+=n;self.consumed+=n;
        }
        if(framesRead) *framesRead=done;return done>0?MA_SUCCESS:status;
    }
    static ma_result Seek(ma_data_source* p,ma_uint64 frame) {auto& self=Self(p);frame=std::min(frame,self.length);const auto result=ma_decoder_seek_to_pcm_frame(&self.decoder,frame);if(result==MA_SUCCESS){self.ResetEffect();self.consumed=frame;}return result;}
    static ma_result Format(ma_data_source* p,ma_format* format,ma_uint32* channelsOut,ma_uint32* rateOut,ma_channel* map,size_t capacity) {auto& self=Self(p);if(format) *format=ma_format_f32;if(channelsOut)*channelsOut=2;if(rateOut)*rateOut=self.rate;if(map&&capacity>=2){map[0]=MA_CHANNEL_FRONT_LEFT;map[1]=MA_CHANNEL_FRONT_RIGHT;}return MA_SUCCESS;}
    static ma_result Cursor(ma_data_source* p,ma_uint64* frame) {if(!frame)return MA_INVALID_ARGS;auto& self=Self(p);*frame=self.looping.load(std::memory_order_relaxed)&&self.length?self.consumed%self.length:std::min(self.consumed,self.length);return MA_SUCCESS;}
    static ma_result Length(ma_data_source* p,ma_uint64* frame) {if(!frame)return MA_INVALID_ARGS;*frame=Self(p).length;return MA_SUCCESS;}
    static ma_result Loop(ma_data_source* p,ma_bool32 loop){Self(p).looping.store(loop!=MA_FALSE,std::memory_order_relaxed);return MA_SUCCESS;}
};
#else
struct MiniaudioSpatialStream::Impl {};
#endif
MiniaudioSpatialStream::MiniaudioSpatialStream()=default;
MiniaudioSpatialStream::~MiniaudioSpatialStream()=default;
ma_result MiniaudioSpatialStream::Initialize(ma_engine& engine,const ResolvedAudioClip& clip,kb::audio::SpatialAudioMode mode,ma_uint64 initialFrame) {
#if KB_AUDIO_HAS_STEAMAUDIO
    if(mode!=kb::audio::SpatialAudioMode::Binaural&&mode!=kb::audio::SpatialAudioMode::AmbisonicsFirstOrder) return MA_INVALID_ARGS;
    if(ma_engine_get_listener_count(&engine)!=1) return MA_NOT_IMPLEMENTED;
    auto state=std::make_unique<Impl>();state->engine=&engine;state->mode=mode;state->rate=ma_engine_get_sample_rate(&engine);state->channels=mode==kb::audio::SpatialAudioMode::Binaural?1U:4U;
    auto config=ma_decoder_config_init(ma_format_f32,0,state->rate);config.encodingFormat=EncodingFormatForAudioExtension(clip.extension);
    ma_result result=MA_ERROR;
    if(clip.IsMemoryBacked()){const auto bytes=clip.EncodedBytes();state->payload=clip.EncodedOwner();result=ma_decoder_init_memory(bytes.data(),bytes.size(),&config,&state->decoder);}
    else {
#if defined(_WIN32)
        result=ma_decoder_init_file_w(clip.path.c_str(),&config,&state->decoder);
#else
        result=ma_decoder_init_file(clip.path.c_str(),&config,&state->decoder);
#endif
    }
    if(result!=MA_SUCCESS) return result;state->decoderReady=true;
    ma_uint32 channels=0;result=ma_decoder_get_data_format(&state->decoder,nullptr,&channels,nullptr,nullptr,0);if(result!=MA_SUCCESS)return result;if(channels!=state->channels) return MA_INVALID_ARGS;
    result=ma_decoder_get_length_in_pcm_frames(&state->decoder,&state->length);if(result!=MA_SUCCESS||state->length==0) return MA_INVALID_ARGS;
    state->shared=SpatialContext::Acquire(state->rate);if(!state->shared) return MA_ERROR;
    if(iplAudioBufferAllocate(state->shared->context,static_cast<IPLint32>(state->channels),static_cast<IPLint32>(kFrames),&state->inputBuffer)!=IPL_STATUS_SUCCESS||
       iplAudioBufferAllocate(state->shared->context,2,static_cast<IPLint32>(kFrames),&state->outputBuffer)!=IPL_STATUS_SUCCESS) return MA_OUT_OF_MEMORY;
    if(mode==kb::audio::SpatialAudioMode::AmbisonicsFirstOrder&&iplAudioBufferAllocate(state->shared->context,4,static_cast<IPLint32>(kFrames),&state->nativeBuffer)!=IPL_STATUS_SUCCESS) return MA_OUT_OF_MEMORY;
    if(mode==kb::audio::SpatialAudioMode::Binaural){IPLBinauralEffectSettings settings{state->shared->hrtf};if(iplBinauralEffectCreate(state->shared->context,&state->shared->audio,&settings,&state->binaural)!=IPL_STATUS_SUCCESS) return MA_ERROR;}
    else {IPLAmbisonicsDecodeEffectSettings settings{};settings.speakerLayout.type=IPL_SPEAKERLAYOUTTYPE_STEREO;settings.hrtf=state->shared->hrtf;settings.maxOrder=1;if(iplAmbisonicsDecodeEffectCreate(state->shared->context,&state->shared->audio,&settings,&state->ambisonics)!=IPL_STATUS_SUCCESS)return MA_ERROR;}
    static const ma_data_source_vtable vtable{&Impl::Read,&Impl::Seek,&Impl::Format,&Impl::Cursor,&Impl::Length,&Impl::Loop,MA_DATA_SOURCE_SELF_MANAGED_RANGE_AND_LOOP_POINT};
    auto source=ma_data_source_config_init();source.vtable=&vtable;result=ma_data_source_init(&source,&state->base);if(result!=MA_SUCCESS)return result;state->baseReady=true;
    if(initialFrame){result=Impl::Seek(&state->base,initialFrame);if(result!=MA_SUCCESS)return result;}
    impl_=std::move(state);return MA_SUCCESS;
#else
    static_cast<void>(engine);static_cast<void>(clip);static_cast<void>(mode);static_cast<void>(initialFrame);return MA_NOT_IMPLEMENTED;
#endif
}
ma_data_source* MiniaudioSpatialStream::DataSource() noexcept {
#if KB_AUDIO_HAS_STEAMAUDIO
    return impl_?&impl_->base:nullptr;
#else
    return nullptr;
#endif
}
void MiniaudioSpatialStream::SetPosition(kb::scene::Vec3 p) noexcept {
#if KB_AUDIO_HAS_STEAMAUDIO
    if(impl_){impl_->px.store(p.x,std::memory_order_relaxed);impl_->py.store(p.y,std::memory_order_relaxed);impl_->pz.store(p.z,std::memory_order_relaxed);}
#else
    static_cast<void>(p);
#endif
}
void MiniaudioSpatialStream::Update(const MiniaudioSoundSettings& settings) noexcept {
    SetPosition(settings.position);
#if KB_AUDIO_HAS_STEAMAUDIO
    if(impl_){impl_->blend.store(settings.spatialBlend,std::memory_order_relaxed);impl_->minimum.store(settings.minDistance,std::memory_order_relaxed);impl_->maximum.store(settings.maxDistance,std::memory_order_relaxed);impl_->rolloff.store(settings.rolloff,std::memory_order_relaxed);impl_->attenuation.store(Attenuation(settings.attenuationModel),std::memory_order_relaxed);}
#else
    static_cast<void>(settings);
#endif
}
}
