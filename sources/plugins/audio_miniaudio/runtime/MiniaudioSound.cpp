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

void MiniaudioSound::Stop() noexcept {
    if (initialized_) {
        static_cast<void>(ma_sound_stop(&sound_));
    }
    if (flatInitialized_) {
        static_cast<void>(ma_sound_stop(&flatSound_));
    }
}

bool MiniaudioSound::IsPlaying() const noexcept {
    return initialized_ &&
        (ma_sound_is_playing(&sound_) != MA_FALSE ||
            (flatInitialized_ && ma_sound_is_playing(const_cast<ma_sound*>(&flatSound_)) != MA_FALSE));
}

ma_result MiniaudioSound::SeekSeconds(float positionSeconds) noexcept {
    if (!initialized_) {
        return MA_INVALID_OPERATION;
    }
    const ma_engine* engine = ma_sound_get_engine(const_cast<ma_sound*>(&sound_));
    const ma_uint32 sampleRate = engine == nullptr ? 0U : ma_engine_get_sample_rate(const_cast<ma_engine*>(engine));
    if (sampleRate == 0U) {
        return MA_INVALID_OPERATION;
    }
    const float clamped = std::max(0.0F, ValidOr(positionSeconds, 0.0F));
    const ma_uint64 frame = static_cast<ma_uint64>(static_cast<double>(clamped) * sampleRate);
    const ma_result primaryResult = ma_sound_seek_to_pcm_frame(&sound_, frame);
    if (primaryResult != MA_SUCCESS || !flatInitialized_) {
        return primaryResult;
    }
    return ma_sound_seek_to_pcm_frame(&flatSound_, frame);
}

void MiniaudioSound::SetVolume(float volume) noexcept {
    if (initialized_) {
        volume_ = std::max(0.0F, ValidOr(volume, 1.0F));
        ApplyVolumes();
    }
}

void MiniaudioSound::SetPitch(float pitch) noexcept {
    if (initialized_) {
        ma_sound_set_pitch(&sound_, std::max(0.01F, ValidOr(pitch, 1.0F)));
        if (flatInitialized_) {
            ma_sound_set_pitch(&flatSound_, std::max(0.01F, ValidOr(pitch, 1.0F)));
        }
    }
}

void MiniaudioSound::SetLooping(bool loop) noexcept {
    if (initialized_) {
        ma_sound_set_looping(&sound_, loop ? MA_TRUE : MA_FALSE);
        if (flatInitialized_) {
            ma_sound_set_looping(&flatSound_, loop ? MA_TRUE : MA_FALSE);
        }
    }
}

void MiniaudioSound::SetPosition(const kb::scene::Vec3& position) noexcept {
    if (initialized_) {
        ma_sound_set_position(&sound_, ValidOr(position.x, 0.0F), ValidOr(position.y, 0.0F), ValidOr(position.z, 0.0F));
    }
}

float MiniaudioSound::PlaybackSeconds() const noexcept {
    if (!initialized_) {
        return -1.0F;
    }
    const ma_engine* engine = ma_sound_get_engine(const_cast<ma_sound*>(&sound_));
    const ma_uint32 sampleRate = engine == nullptr ? 0U : ma_engine_get_sample_rate(const_cast<ma_engine*>(engine));
    if (sampleRate == 0U) {
        return -1.0F;
    }
    return static_cast<float>(static_cast<double>(ma_sound_get_time_in_pcm_frames(&sound_)) / sampleRate);
}

ma_result MiniaudioSound::InitializeFromFile(ma_engine& engine, const std::filesystem::path& path, bool spatial, ma_sound_group* group) {
    Reset();
    const ma_result result = InitSoundFromFile(engine, path, SoundFlags(spatial), group, sound_);
    initialized_ = result == MA_SUCCESS;
    if (!initialized_ || !spatial) {
        return result;
    }
    const ma_result flatResult = InitSoundFromFile(engine, path, SoundFlags(false), group, flatSound_);
    flatInitialized_ = flatResult == MA_SUCCESS;
    if (!flatInitialized_) {
        ma_sound_uninit(&sound_);
        initialized_ = false;
        return flatResult;
    }
    return result;
}

void MiniaudioSound::Reset() noexcept {
    if (flatInitialized_) {
        static_cast<void>(ma_sound_stop(&flatSound_));
        ma_sound_uninit(&flatSound_);
        flatInitialized_ = false;
    }
    if (initialized_) {
        static_cast<void>(ma_sound_stop(&sound_));
        ma_sound_uninit(&sound_);
        initialized_ = false;
    }
    volume_ = 1.0F;
    spatialBlend_ = 1.0F;
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

    volume_ = volume;
    spatialBlend_ = flatInitialized_ && settings.spatial ? spatialBlend : 1.0F;
    ApplyVolumes();
    ma_sound_set_pitch(&sound_, pitch);
    ma_sound_set_pan(&sound_, flatInitialized_ ? 0.0F : pan);
    ma_sound_set_looping(&sound_, settings.loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_spatialization_enabled(&sound_, settings.spatial ? MA_TRUE : MA_FALSE);
    ma_sound_set_attenuation_model(&sound_, ToMiniaudioAttenuationModel(settings.attenuationModel));
    ma_sound_set_min_distance(&sound_, minDistance);
    ma_sound_set_max_distance(&sound_, maxDistance);
    ma_sound_set_rolloff(&sound_, rolloff);
    ma_sound_set_doppler_factor(&sound_, dopplerFactor);
    ma_sound_set_position(&sound_, settings.position.x, settings.position.y, settings.position.z);
    if (flatInitialized_) {
        ma_sound_set_pitch(&flatSound_, pitch);
        ma_sound_set_pan(&flatSound_, pan);
        ma_sound_set_looping(&flatSound_, settings.loop ? MA_TRUE : MA_FALSE);
        ma_sound_set_spatialization_enabled(&flatSound_, MA_FALSE);
    }
}

ma_result MiniaudioSound::Start() noexcept {
    if (!initialized_) {
        return MA_INVALID_OPERATION;
    }
    const ma_result primaryResult = ma_sound_start(&sound_);
    if (primaryResult != MA_SUCCESS || !flatInitialized_) {
        return primaryResult;
    }
    const ma_result flatResult = ma_sound_start(&flatSound_);
    if (flatResult != MA_SUCCESS) {
        static_cast<void>(ma_sound_stop(&sound_));
    }
    return flatResult;
}

bool MiniaudioSound::AtEnd() const noexcept {
    return initialized_ && ma_sound_at_end(&sound_) == MA_TRUE;
}

bool MiniaudioSound::IsInitialized() const noexcept {
    return initialized_;
}

void MiniaudioSound::ApplyVolumes() noexcept {
    if (!initialized_) {
        return;
    }
    if (!flatInitialized_) {
        ma_sound_set_volume(&sound_, volume_);
        return;
    }
    ma_sound_set_volume(&sound_, volume_ * spatialBlend_);
    ma_sound_set_volume(&flatSound_, volume_ * (1.0F - spatialBlend_));
}

} // namespace kb::audio_miniaudio
