#include "engine/audio/SpatialAudioPlayback.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include <cassert>
#include <thread>
namespace kb::audio {
std::string ValidateSpatialAudioPlayDesc(const kb::scene::Scene& scene,const AudioPlayDesc& desc,SpatialAudioMode mode) {
    const auto base=ValidateAudioPlayDesc(scene,desc);
    if(base!=AudioPlayDescValidationStatus::Valid) return AudioPlayDescValidationResult(base).error;
    if(mode!=SpatialAudioMode::Panning&&mode!=SpatialAudioMode::Binaural&&mode!=SpatialAudioMode::AmbisonicsFirstOrder) return "unknown spatial audio mode";
    if(mode==SpatialAudioMode::Panning) return {};
    if(!desc.spatial||desc.dopplerFactor!=0) return "binaural playback requires spatial=true and dopplerFactor=0";
    if(mode==SpatialAudioMode::AmbisonicsFirstOrder&&
        (desc.ownerEntityId!=0||desc.position.x!=0||desc.position.y!=0||desc.position.z!=0||
         desc.velocity.x!=0||desc.velocity.y!=0||desc.velocity.z!=0||desc.attenuationModel!=AudioAttenuationModel::None||desc.spatialBlend!=1))
        return "AmbiX soundfields require origin position, no owner/velocity/distance attenuation and spatialBlend=1; listener orientation rotates the field";
    return {};
}
AudioPlayResult SpatialAudioPlayback::Play(kb::scene::Scene& scene,const AudioPlayDesc& desc,SpatialAudioMode mode) {
    if(const auto error=ValidateSpatialAudioPlayDesc(scene,desc,mode);!error.empty()) return {.started=false,.voiceId=0,.error=error};
    if(mode==SpatialAudioMode::Panning) return AudioPlayback::PlayOneShot(scene,desc);
    auto& state=kb::scene::SceneAccess::State(scene);
#if !defined(NDEBUG)
    assert(state.audioPlaybackOwnerThread==std::this_thread::get_id()&&"audio playback must be accessed from its scene owner thread");
#endif
    auto* spatial=dynamic_cast<ISpatialAudioPlaybackBackend*>(state.audioPlaybackBackend);
    if(!spatial) return {.started=false,.voiceId=0,.error="active audio provider does not support binaural or AmbiX playback"};
    return spatial->PlaySpatialAudio(scene,desc,mode);
}
}
