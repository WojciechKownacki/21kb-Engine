#include "runtime/MiniaudioSound.hpp"

#include "assets/MiniaudioEncodingFormat.hpp"
#include "assets/ResolvedAudioClip.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <span>
#include <string>

namespace kb::audio_miniaudio {

struct MiniaudioSound::MemoryDecoderState final {
    ma_decoder decoder{};
    ma_decoder flatDecoder{};
    std::shared_ptr<const kb::assets::ImportedAsset> payload;
    bool decoderInitialized = false;
    bool flatDecoderInitialized = false;

    ~MemoryDecoderState() {
        if (flatDecoderInitialized) {
            ma_decoder_uninit(&flatDecoder);
        }
        if (decoderInitialized) {
            ma_decoder_uninit(&decoder);
        }
    }
};

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

[[nodiscard]] ma_result InitDecoder(
    std::span<const std::byte> bytes,
    std::string_view extension,
    ma_decoder& decoder) {
    const ma_encoding_format encodingFormat = EncodingFormatForAudioExtension(extension);
    if (encodingFormat == ma_encoding_format_unknown) {
        return MA_INVALID_ARGS;
    }
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0U, 0U);
    config.encodingFormat = encodingFormat;
    return ma_decoder_init_memory(bytes.data(), bytes.size(), &config, &decoder);
}

[[nodiscard]] ma_result InitStreamingSoundFromFile(
    ma_engine& engine,
    const std::filesystem::path& path,
    ma_sound_group* group,
    ma_uint64 initialFrame,
    ma_sound& sound) {
    ma_sound_config config = ma_sound_config_init_2(&engine);
    config.flags = MA_SOUND_FLAG_STREAM;
    config.pInitialAttachment = group;
    config.initialSeekPointInPCMFrames = initialFrame;
#if defined(_WIN32)
    const std::wstring nativePath = path.wstring();
    config.pFilePathW = nativePath.c_str();
    return ma_sound_init_ex(&engine, &config, &sound);
#else
    const std::string nativePath = path.string();
    config.pFilePath = nativePath.c_str();
    return ma_sound_init_ex(&engine, &config, &sound);
#endif
}

[[nodiscard]] ma_result InitSoundFromDecoder(
    ma_engine& engine,
    ma_decoder& decoder,
    ma_sound_group* group,
    ma_sound& sound) {
    return ma_sound_init_from_data_source(
        &engine, reinterpret_cast<ma_data_source*>(&decoder), 0U, group, &sound);
}

} // namespace

MiniaudioSound::MiniaudioSound() = default;

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
    ma_uint32 sampleRate = 0U;
    if (ma_sound_get_data_format(&sound_, nullptr, nullptr, &sampleRate, nullptr, 0U) != MA_SUCCESS || sampleRate == 0U) {
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
        volume_ = NormalizeVolume(volume);
        ApplyVolumes();
    }
}

void MiniaudioSound::SetMute(bool mute) noexcept {
    if (initialized_) {
        muted_ = mute;
        ApplyVolumes();
    }
}

void MiniaudioSound::SetPan(float pan) noexcept {
    if (!initialized_) {
        return;
    }
    const float normalized = NormalizePan(pan);
    ma_sound_set_pan(&sound_, flatInitialized_ ? 0.0F : normalized);
    if (flatInitialized_) {
        ma_sound_set_pan(&flatSound_, normalized);
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

void MiniaudioSound::SetVelocity(const kb::scene::Vec3& velocity) noexcept {
    if (initialized_) {
        ma_sound_set_velocity(&sound_, ValidOr(velocity.x, 0.0F), ValidOr(velocity.y, 0.0F), ValidOr(velocity.z, 0.0F));
    }
}

float MiniaudioSound::NormalizeVolume(float volume) noexcept {
    return std::max(0.0F, ValidOr(volume, 1.0F));
}

float MiniaudioSound::NormalizePan(float pan) noexcept {
    return std::clamp(ValidOr(pan, 0.0F), -1.0F, 1.0F);
}

float MiniaudioSound::PlaybackSeconds() const noexcept {
    if (!initialized_) {
        return -1.0F;
    }
    ma_uint32 sampleRate = 0U;
    if (ma_sound_get_data_format(&sound_, nullptr, nullptr, &sampleRate, nullptr, 0U) != MA_SUCCESS || sampleRate == 0U) {
        return -1.0F;
    }
    return static_cast<float>(static_cast<double>(PlaybackFrame()) / sampleRate);
}

ma_uint64 MiniaudioSound::PlaybackFrame() const noexcept {
    if (!initialized_ || sound_.pDataSource == nullptr) {
        return 0U;
    }
    ma_uint64 frame = 0U;
    return ma_data_source_get_cursor_in_pcm_frames(sound_.pDataSource, &frame) == MA_SUCCESS ? frame : 0U;
}

ma_result MiniaudioSound::Initialize(
    ma_engine& engine,
    const ResolvedAudioClip& clip,
    bool spatial,
    ma_sound_group* group,
    ma_uint64 initialFrame) {
    Reset();
    if (!clip.IsValid()) {
        return MA_INVALID_ARGS;
    }
    if (!clip.IsMemoryBacked()) {
        ma_result result = InitStreamingSoundFromFile(
            engine, clip.path, group, initialFrame, sound_);
        initialized_ = result == MA_SUCCESS;
        if (!initialized_ || !spatial) {
            if (!initialized_) {
                Reset();
            }
            return result;
        }
        result = InitStreamingSoundFromFile(
            engine, clip.path, group, initialFrame, flatSound_);
        flatInitialized_ = result == MA_SUCCESS;
        if (!flatInitialized_) {
            Reset();
            return result;
        }
        return MA_SUCCESS;
    }
    memoryDecoder_ = std::make_unique<MemoryDecoderState>();
    memoryDecoder_->payload = clip.imported;
    ma_result result = InitDecoder(
        clip.EncodedBytes(), clip.extension, memoryDecoder_->decoder);
    memoryDecoder_->decoderInitialized = result == MA_SUCCESS;
    if (!memoryDecoder_->decoderInitialized) {
        Reset();
        return result;
    }
    if (initialFrame != 0U) {
        result = ma_decoder_seek_to_pcm_frame(&memoryDecoder_->decoder, initialFrame);
        if (result != MA_SUCCESS) {
            Reset();
            return result;
        }
    }
    result = InitSoundFromDecoder(engine, memoryDecoder_->decoder, group, sound_);
    initialized_ = result == MA_SUCCESS;
    if (!initialized_ || !spatial) {
        if (!initialized_) {
            Reset();
        }
        return result;
    }
    result = InitDecoder(
        clip.EncodedBytes(), clip.extension, memoryDecoder_->flatDecoder);
    memoryDecoder_->flatDecoderInitialized = result == MA_SUCCESS;
    if (!memoryDecoder_->flatDecoderInitialized) {
        Reset();
        return result;
    }
    if (initialFrame != 0U) {
        result = ma_decoder_seek_to_pcm_frame(&memoryDecoder_->flatDecoder, initialFrame);
        if (result != MA_SUCCESS) {
            Reset();
            return result;
        }
    }
    const ma_result flatResult = InitSoundFromDecoder(
        engine, memoryDecoder_->flatDecoder, group, flatSound_);
    flatInitialized_ = flatResult == MA_SUCCESS;
    if (!flatInitialized_) {
        Reset();
        return flatResult;
    }
    return MA_SUCCESS;
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
    memoryDecoder_.reset();
    volume_ = 1.0F;
    muted_ = false;
    spatialBlend_ = 1.0F;
}

#if defined(KB_AUDIO_MINIAUDIO_TESTING)
std::size_t MiniaudioSound::DecoderCountForTesting() const noexcept {
    return memoryDecoder_ == nullptr
        ? 0U
        : (memoryDecoder_->decoderInitialized ? 1U : 0U)
            + (memoryDecoder_->flatDecoderInitialized ? 1U : 0U);
}

bool MiniaudioSound::OwnsEncodedPayloadForTesting() const noexcept {
    return memoryDecoder_ != nullptr && memoryDecoder_->payload != nullptr;
}

bool MiniaudioSound::UsesResourceManagerStreamForTesting() const noexcept {
    return initialized_ && memoryDecoder_ == nullptr;
}
#endif

void MiniaudioSound::Apply(const MiniaudioSoundSettings& settings) noexcept {
    if (!initialized_) {
        return;
    }
    const float volume = NormalizeVolume(settings.volume);
    const float pitch = std::max(0.01F, ValidOr(settings.pitch, 1.0F));
    const float pan = NormalizePan(settings.pan);
    const float spatialBlend = std::clamp(ValidOr(settings.spatialBlend, 1.0F), 0.0F, 1.0F);
    const float minDistance = std::max(0.01F, ValidOr(settings.minDistance, 1.0F));
    const float maxDistance = std::max(minDistance, ValidOr(settings.maxDistance, 500.0F));
    const float rolloff = std::max(0.0F, ValidOr(settings.rolloff, 1.0F));
    const float dopplerFactor = std::max(0.0F, ValidOr(settings.dopplerFactor, 1.0F));

    volume_ = volume;
    muted_ = settings.mute;
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
    ma_sound_set_position(&sound_, ValidOr(settings.position.x, 0.0F), ValidOr(settings.position.y, 0.0F), ValidOr(settings.position.z, 0.0F));
    ma_sound_set_velocity(&sound_, ValidOr(settings.velocity.x, 0.0F), ValidOr(settings.velocity.y, 0.0F), ValidOr(settings.velocity.z, 0.0F));
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
        ma_sound_set_volume(&sound_, muted_ ? 0.0F : volume_);
        return;
    }
    const float volume = muted_ ? 0.0F : volume_;
    ma_sound_set_volume(&sound_, volume * spatialBlend_);
    ma_sound_set_volume(&flatSound_, volume * (1.0F - spatialBlend_));
}

} // namespace kb::audio_miniaudio
