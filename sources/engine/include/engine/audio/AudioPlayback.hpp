#pragma once

#include "engine/audio/AudioSettings.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cstdint>
#include <string>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::audio {

struct AudioPlayDesc {
    std::uint64_t clipAssetId = 0U;
    // LIB-147: names an AudioMixerBus of the scene's active AudioMixer asset
    // (SceneAudioMixerAccess); empty or unknown routes to the implicit master output -
    // the same honest fallback AudioSourceComponent::outputBus documents.
    std::string outputBus;
    float volume = 1.0F;
    float pitch = 1.0F;
    bool mute = false;
    bool loop = false;
    bool spatial = true;
    float pan = 0.0F;
    float spatialBlend = 1.0F;
    AudioAttenuationModel attenuationModel = AudioAttenuationModel::Inverse;
    float minDistance = 1.0F;
    float maxDistance = 500.0F;
    float rolloff = 1.0F;
    float dopplerFactor = 1.0F;
    kb::scene::Vec3 position{};
    // LIB-148: voice-stealing priority. When the one-shot pool is full, the LOWEST
    // priority voice is evicted first (ties evict the oldest); a new request that is
    // itself lower-priority than every live voice is honestly refused instead of
    // stealing. 128 = neutral default (every pre-LIB-148 caller keeps it).
    std::uint8_t priority = 128U;
};

struct AudioPlayResult {
    bool started = false;
    std::uint64_t voiceId = 0U;
    std::string error;

    [[nodiscard]] bool Succeeded() const noexcept {
        return started && voiceId != 0U && error.empty();
    }
};

class IAudioPlaybackBackend {
public:
    virtual ~IAudioPlaybackBackend() = default;

    [[nodiscard]] virtual AudioPlayResult PlayOneShot(kb::scene::Scene& scene, const AudioPlayDesc& desc) = 0;
    virtual void StopAll(kb::scene::Scene& scene) noexcept = 0;
    // LIB-148: per-voice control of a PlayOneShot result. Every call returns false for a
    // voiceId that is 0, never existed, or already finished/was stolen (a one-shot voice's
    // lifetime is honest - no operation resurrects it). Pause keeps the play cursor;
    // Resume continues from it; Seek positions in SECONDS from the clip start (clamped by
    // the backend); volume/pitch/loop mirror AudioPlayDesc's semantics live.
    [[nodiscard]] virtual bool StopVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept = 0;
    [[nodiscard]] virtual bool PauseVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept = 0;
    [[nodiscard]] virtual bool ResumeVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept = 0;
    [[nodiscard]] virtual bool SeekVoice(kb::scene::Scene& scene, std::uint64_t voiceId, float positionSeconds) noexcept = 0;
    [[nodiscard]] virtual bool SetVoiceVolume(kb::scene::Scene& scene, std::uint64_t voiceId, float volume) noexcept = 0;
    [[nodiscard]] virtual bool SetVoicePitch(kb::scene::Scene& scene, std::uint64_t voiceId, float pitch) noexcept = 0;
    [[nodiscard]] virtual bool SetVoiceLoop(kb::scene::Scene& scene, std::uint64_t voiceId, bool loop) noexcept = 0;
    [[nodiscard]] virtual bool IsVoicePlaying(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept = 0;
};

class AudioPlayback final {
public:
    AudioPlayback() = delete;

    static void RegisterBackend(kb::scene::Scene& scene, IAudioPlaybackBackend& backend);
    static void UnregisterBackend(kb::scene::Scene& scene, IAudioPlaybackBackend& backend) noexcept;
    [[nodiscard]] static bool HasBackend(kb::scene::Scene& scene) noexcept;
    [[nodiscard]] static AudioPlayResult PlayOneShot(kb::scene::Scene& scene, const AudioPlayDesc& desc);
    static void StopAll(kb::scene::Scene& scene) noexcept;
    // LIB-148: per-voice facade - false when no backend is registered or the backend
    // reports the voice dead (see IAudioPlaybackBackend's own contract above).
    [[nodiscard]] static bool StopVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept;
    [[nodiscard]] static bool PauseVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept;
    [[nodiscard]] static bool ResumeVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept;
    [[nodiscard]] static bool SeekVoice(kb::scene::Scene& scene, std::uint64_t voiceId, float positionSeconds) noexcept;
    [[nodiscard]] static bool SetVoiceVolume(kb::scene::Scene& scene, std::uint64_t voiceId, float volume) noexcept;
    [[nodiscard]] static bool SetVoicePitch(kb::scene::Scene& scene, std::uint64_t voiceId, float pitch) noexcept;
    [[nodiscard]] static bool SetVoiceLoop(kb::scene::Scene& scene, std::uint64_t voiceId, bool loop) noexcept;
    [[nodiscard]] static bool IsVoicePlaying(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept;
};

} // namespace kb::audio
