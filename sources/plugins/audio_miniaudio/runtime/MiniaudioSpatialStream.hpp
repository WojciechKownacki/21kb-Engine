#pragma once
#include "engine/audio/SpatialAudioPlayback.hpp"
#include <miniaudio.h>
#include <memory>
namespace kb::audio_miniaudio {
struct ResolvedAudioClip;
struct MiniaudioSoundSettings;
class MiniaudioSpatialStream final {
public:
    MiniaudioSpatialStream();
    ~MiniaudioSpatialStream();
    [[nodiscard]] ma_result Initialize(ma_engine&,const ResolvedAudioClip&,kb::audio::SpatialAudioMode,ma_uint64 initialFrame);
    [[nodiscard]] ma_data_source* DataSource() noexcept;
    void Update(const MiniaudioSoundSettings&) noexcept;
    void SetPosition(kb::scene::Vec3) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
