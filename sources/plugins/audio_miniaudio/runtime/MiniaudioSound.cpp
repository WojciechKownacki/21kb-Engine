#include "runtime/MiniaudioSound.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace kb::audio_miniaudio {
namespace {

[[nodiscard]] ma_attenuation_model ToMiniaudioAttenuationModel(kb::audio::AudioAttenuationModel model) noexcept {
    switch (model) {
    case kb::audio::AudioAttenuationModel::None:
        return ma_attenuation_model_none;
    case kb::audio::AudioAttenuationModel::Linear:
        return ma_attenuation_model_linear;
    case kb::audio::AudioAttenuationModel::Exponential:
        return ma_attenuation_model_exponential;
    case kb::audio::AudioAttenuationModel::Inverse:
    default:
        return ma_attenuation_model_inverse;
    }
}

[[nodiscard]] float ValidOr(float value, float fallback) noexcept {
    return std::isfinite(value) ? value : fallback;
}

[[nodiscard]] ma_uint32 SoundFlags(bool spatial) noexcept {
    static_cast<void>(spatial);
    return MA_SOUND_FLAG_STREAM;
}

[[nodiscard]] ma_result InitSoundFromFile(ma_engine& engine, const std::filesystem::path& path, ma_uint32 flags, ma_sound_group* group, ma_sound& sound) {
#if defined(_WIN32)
    const std::wstring nativePath = path.wstring();
    return ma_sound_init_from_file_w(&engine, nativePath.c_str(), flags, group, nullptr, &sound);
#else
    const std::string nativePath = path.string();
    return ma_sound_init_from_file(&engine, nativePath.c_str(), flags, group, nullptr, &sound);
#endif
}

} // namespace

MiniaudioSound::~MiniaudioSound() {
    Reset();
}

ma_result MiniaudioSound::InitializeFromFile(ma_engine& engine, const std::filesystem::path& path, bool spatial, ma_sound_group* group) {
    Reset();
    const ma_result result = InitSoundFromFile(engine, path, SoundFlags(spatial), group, sound_);
    initialized_ = result == MA_SUCCESS;
    return result;
}

void MiniaudioSound::Reset() noexcept {
    if (!initialized_) {
        return;
    }
    static_cast<void>(ma_sound_stop(&sound_));
    ma_sound_uninit(&sound_);
    initialized_ = false;
}

void MiniaudioSound::Apply(const MiniaudioSoundSettings& settings) noexcept {
    if (!initialized_) {
        return;
    }
    const float volume = settings.mute ? 0.0F : std::max(0.0F, ValidOr(settings.volume, 1.0F));
    const float pitch = std::max(0.01F, ValidOr(settings.pitch, 1.0F));
    const float pan = std::clamp(ValidOr(settings.pan, 0.0F), -1.0F, 1.0F);
    const float spatialBlend = std::clamp(ValidOr(settings.spatialBlend, 1.0F), 0.0F, 1.0F);
    const float minDistance = std::max(0.01F, ValidOr(settings.minDistance, 1.0F));
    const float maxDistance = std::max(minDistance, ValidOr(settings.maxDistance, 500.0F));
    const float rolloff = std::max(0.0F, ValidOr(settings.rolloff, 1.0F));
    const float dopplerFactor = std::max(0.0F, ValidOr(settings.dopplerFactor, 1.0F));

    ma_sound_set_volume(&sound_, volume);
    ma_sound_set_pitch(&sound_, pitch);
    ma_sound_set_pan(&sound_, pan);
    ma_sound_set_looping(&sound_, settings.loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_spatialization_enabled(&sound_, settings.spatial && spatialBlend > 0.0F ? MA_TRUE : MA_FALSE);
    ma_sound_set_attenuation_model(&sound_, ToMiniaudioAttenuationModel(settings.attenuationModel));
    ma_sound_set_min_distance(&sound_, minDistance);
    ma_sound_set_max_distance(&sound_, maxDistance);
    ma_sound_set_rolloff(&sound_, rolloff);
    ma_sound_set_doppler_factor(&sound_, dopplerFactor);
    ma_sound_set_position(&sound_, settings.position.x, settings.position.y, settings.position.z);
}

ma_result MiniaudioSound::Start() noexcept {
    if (!initialized_) {
        return MA_INVALID_OPERATION;
    }
    return ma_sound_start(&sound_);
}

bool MiniaudioSound::AtEnd() const noexcept {
    return initialized_ && ma_sound_at_end(&sound_) == MA_TRUE;
}

bool MiniaudioSound::IsInitialized() const noexcept {
    return initialized_;
}

} // namespace kb::audio_miniaudio
