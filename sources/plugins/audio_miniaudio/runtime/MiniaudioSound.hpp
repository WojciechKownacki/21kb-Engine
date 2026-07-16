#pragma once

#include "engine/audio/AudioSettings.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <miniaudio.h>

#include <filesystem>

namespace kb::audio_miniaudio {

struct MiniaudioSoundSettings {
    float volume = 1.0F;
    float pitch = 1.0F;
    bool mute = false;
    bool loop = false;
    bool spatial = true;
    float pan = 0.0F;
    float spatialBlend = 1.0F;
    kb::audio::AudioAttenuationModel attenuationModel = kb::audio::AudioAttenuationModel::Inverse;
    float minDistance = 1.0F;
    float maxDistance = 500.0F;
    float rolloff = 1.0F;
    float dopplerFactor = 1.0F;
    kb::scene::Vec3 position{};
};

class MiniaudioSound final {
public:
    MiniaudioSound() = default;
    ~MiniaudioSound();

    MiniaudioSound(const MiniaudioSound&) = delete;
    MiniaudioSound& operator=(const MiniaudioSound&) = delete;
    MiniaudioSound(MiniaudioSound&&) = delete;
    MiniaudioSound& operator=(MiniaudioSound&&) = delete;

    // LIB-147: `group` attaches the sound to a mixer bus (nullptr = the engine's own
    // endpoint, the implicit master - the pre-mixer behavior).
    [[nodiscard]] ma_result InitializeFromFile(ma_engine& engine, const std::filesystem::path& path, bool spatial, ma_sound_group* group = nullptr);
    void Reset() noexcept;

    void Apply(const MiniaudioSoundSettings& settings) noexcept;
    [[nodiscard]] ma_result Start() noexcept;
    [[nodiscard]] bool AtEnd() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    // LIB-148: per-voice control. Pause() stops the device-side playback WITHOUT moving
    // the play cursor, so Start() (resume) continues where it left off; SeekSeconds
    // positions from the clip start (clamped by miniaudio); the single-field setters
    // mirror Apply's per-field semantics live.
    void Stop() noexcept;
    [[nodiscard]] bool IsPlaying() const noexcept;
    [[nodiscard]] ma_result SeekSeconds(float positionSeconds) noexcept;
    void SetVolume(float volume) noexcept;
    void SetPitch(float pitch) noexcept;
    void SetLooping(bool loop) noexcept;
    // LIB-149: per-tick position update for owner-attached voices.
    void SetPosition(const kb::scene::Vec3& position) noexcept;
    // LIB-152: the playback position on the AUDIO clock (pcm frames / engine sample
    // rate), in seconds; negative when uninitialized.
    [[nodiscard]] float PlaybackSeconds() const noexcept;

private:
    ma_sound sound_{};
    bool initialized_ = false;
};

} // namespace kb::audio_miniaudio
