#pragma once
#include "engine/audio/AudioPlayback.hpp"
namespace kb::audio {
enum class SpatialAudioMode : std::uint8_t { Panning, Binaural, AmbisonicsFirstOrder };
// Optional capability of the existing playback backend, preserving AudioPlayDesc ABI.
class ISpatialAudioPlaybackBackend {
public:
    virtual ~ISpatialAudioPlaybackBackend()=default;
    [[nodiscard]] virtual AudioPlayResult PlaySpatialAudio(kb::scene::Scene&,const AudioPlayDesc&,SpatialAudioMode)=0;
};
[[nodiscard]] std::string ValidateSpatialAudioPlayDesc(const kb::scene::Scene&,const AudioPlayDesc&,SpatialAudioMode);
class SpatialAudioPlayback final {
public:
    [[nodiscard]] static AudioPlayResult Play(kb::scene::Scene&,const AudioPlayDesc&,SpatialAudioMode);
};
}
