#pragma once

#include "engine/audio/AudioSettings.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::audio {

struct AudioPlayDesc {
    std::uint64_t clipAssetId = 0U;
    // LIB-147: names an AudioMixerBus of the scene's active AudioMixer asset
    // (SceneAudioMixerAccess); empty routes to the implicit master output. A non-empty
    // unknown bus is rejected so authored routing can never silently reach master.
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
    kb::scene::Vec3 velocity{};
    // LIB-148: voice-stealing priority. When the one-shot pool is full, the LOWEST
    // priority voice is evicted first (ties evict the oldest); a new request that is
    // itself lower-priority than every live voice is honestly refused instead of
    // stealing. 128 = neutral default (every pre-LIB-148 caller keeps it).
    std::uint8_t priority = 128U;
    // LIB-149: 0 = free-standing (the pre-LIB-149 behavior: `position` is a one-time
    // start position). Non-zero attaches the voice to that entity: its position follows
    // the owner's world transform every audio tick, and the voice is stopped and its
    // source released the moment the owner is destroyed or deactivated - even a looping
    // voice can never outlive (leak past) its owner, mirroring SceneTimerService's exact
    // owner-gone convention.
    std::uint64_t ownerEntityId = 0U;
};

enum class AudioPlayDescValidationStatus : std::uint8_t {
    Valid,
    InvalidClip,
    InvalidSettings,
    InvalidOwner,
};

struct AudioPlayResult;

// Canonical public validation boundary for one-shot playback. Every public facade and
// backend path rejects a non-Valid request before routing, voice stealing or native
// sound mutation.
[[nodiscard]] AudioPlayDescValidationStatus ValidateAudioPlayDesc(
    const kb::scene::Scene& scene, const AudioPlayDesc& desc) noexcept;
[[nodiscard]] AudioPlayResult AudioPlayDescValidationResult(
    AudioPlayDescValidationStatus status);

inline constexpr std::size_t kMaxAudioVoiceMarkerNameBytes = 255U;

[[nodiscard]] inline bool IsAudioVoiceSeekPositionValid(float positionSeconds) noexcept {
    return std::isfinite(positionSeconds) && positionSeconds >= 0.0F;
}

[[nodiscard]] inline bool IsAudioVoiceVolumeValid(float volume) noexcept {
    return std::isfinite(volume) && volume >= 0.0F;
}

[[nodiscard]] inline bool IsAudioVoicePanValid(float pan) noexcept {
    return std::isfinite(pan) && pan >= -1.0F && pan <= 1.0F;
}

[[nodiscard]] inline bool IsAudioVoicePitchValid(float pitch) noexcept {
    return std::isfinite(pitch) && pitch >= 0.01F;
}

[[nodiscard]] inline bool IsAudioVoiceMarkerNameValid(std::string_view marker) noexcept {
    if (marker.empty() || marker.size() > kMaxAudioVoiceMarkerNameBytes) {
        return false;
    }
    for (const char character : marker) {
        const unsigned char code = static_cast<unsigned char>(character);
        if (code < 0x20U || code == 0x7FU) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsAudioVoiceMarkerRequestValid(
    const kb::scene::Scene& scene,
    std::string_view marker,
    float positionSeconds,
    kb::scene::SceneEntity target) noexcept;

enum class AudioDeviceStatus : std::uint8_t {
    BackendUnavailable,
    Uninitialized,
    NoPlaybackDevice,
    PlaybackAvailable,
};

enum class AudioSourceControlStatus : std::uint8_t {
    Success,
    BackendUnavailable,
    DeviceUnavailable,
    InvalidEntity,
    InactiveEntity,
    MissingComponent,
    Disabled,
    InvalidClip,
    ClipUnavailable,
    MixerUnavailable,
    UnknownBus,
    RoutingInitializationFailed,
    SoundInitializationFailed,
    PlaybackOperationFailed,
    NotPlaying,
    NotPaused,
    InvalidSettings,
};

struct AudioSourceControlResult {
    AudioSourceControlStatus status = AudioSourceControlStatus::BackendUnavailable;
    bool playing = false;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == AudioSourceControlStatus::Success;
    }
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

    // Every lifecycle, playback and control entry is owner-thread-only: the same scene
    // thread that registers the backend must drive it until unregister. Native audio
    // callbacks own only backend/native data; they must never enter this interface or
    // access ECS. A caller originating elsewhere must marshal work to the owner thread.

    [[nodiscard]] virtual AudioPlayResult PlayOneShot(kb::scene::Scene& scene, const AudioPlayDesc& desc) = 0;
    virtual void StopAll(kb::scene::Scene& scene) noexcept = 0;
    [[nodiscard]] virtual AudioSourceControlResult PlaySource(kb::scene::Scene& scene, kb::scene::SceneEntity entity) = 0;
    [[nodiscard]] virtual AudioSourceControlResult PauseSource(kb::scene::Scene& scene, kb::scene::SceneEntity entity) = 0;
    [[nodiscard]] virtual AudioSourceControlResult ResumeSource(kb::scene::Scene& scene, kb::scene::SceneEntity entity) = 0;
    [[nodiscard]] virtual AudioSourceControlResult StopSource(kb::scene::Scene& scene, kb::scene::SceneEntity entity) = 0;
    [[nodiscard]] virtual AudioSourceControlResult IsSourcePlaying(kb::scene::Scene& scene, kb::scene::SceneEntity entity) = 0;
    [[nodiscard]] virtual AudioDeviceStatus DeviceStatus() const noexcept = 0;
    [[nodiscard]] virtual AudioDeviceStatus Reinitialize(kb::scene::Scene& scene) noexcept = 0;
    // LIB-148: per-voice control of a PlayOneShot result. A voiceId that is 0, never
    // existed, was stopped/stolen, or was reclaimed after finishing is never resurrected.
    // Pause succeeds only while the
    // voice is playing and keeps the play cursor; Resume succeeds only while paused and
    // continues from it. Seek positions in SECONDS from the clip start; invalid values
    // are rejected without mutation. Volume/mute/pan/pitch/loop mirror AudioPlayDesc's
    // semantics live.
    [[nodiscard]] virtual bool StopVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept = 0;
    [[nodiscard]] virtual bool PauseVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept = 0;
    [[nodiscard]] virtual bool ResumeVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept = 0;
    [[nodiscard]] virtual bool SeekVoice(kb::scene::Scene& scene, std::uint64_t voiceId, float positionSeconds) noexcept = 0;
    [[nodiscard]] virtual bool SetVoiceVolume(kb::scene::Scene& scene, std::uint64_t voiceId, float volume) noexcept = 0;
    [[nodiscard]] virtual bool SetVoiceMute(kb::scene::Scene& scene, std::uint64_t voiceId, bool mute) noexcept = 0;
    [[nodiscard]] virtual bool SetVoicePan(kb::scene::Scene& scene, std::uint64_t voiceId, float pan) noexcept = 0;
    [[nodiscard]] virtual bool SetVoicePitch(kb::scene::Scene& scene, std::uint64_t voiceId, float pitch) noexcept = 0;
    [[nodiscard]] virtual bool SetVoiceLoop(kb::scene::Scene& scene, std::uint64_t voiceId, bool loop) noexcept = 0;
    [[nodiscard]] virtual bool IsVoicePlaying(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept = 0;
    // LIB-152: the voice's playback position on the AUDIO clock (pcm frames / sample
    // rate), in seconds. Negative for a dead voice - "gone" is distinguishable from
    // "at 0.0" honestly.
    [[nodiscard]] virtual float VoicePlaybackSeconds(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept = 0;
    // LIB-152: registers a named marker on a live voice; when playback crosses
    // `positionSeconds`, the backend queues an "OnAudioMarker" event for `target`
    // (AudioPlayback::QueueMarkerEvent) fired by the script runtime the same frame's
    // event dispatch. False for a dead voice or an empty name.
    [[nodiscard]] virtual bool AddVoiceMarker(kb::scene::Scene& scene, std::uint64_t voiceId, std::string_view marker, float positionSeconds, kb::scene::SceneEntity target) = 0;
};

// LIB-152: one fired voice marker awaiting script dispatch. `target` is the entity whose
// scripts receive the ENTITY-LOCAL "OnAudioMarker" event (the caller that registered the
// marker - the script that asked is the script that gets told). `positionSeconds` is the
// voice's AUDIO-CLOCK playback position at fire time (pcm frames / sample rate) - never
// the wall clock, so gameplay synchronization stays exact under frame hitches.
struct PendingAudioMarkerEvent {
    kb::scene::SceneEntity target{};
    std::uint64_t voiceId = 0U;
    std::string marker;
    float positionSeconds = 0.0F;
};

class AudioPlayback final {
public:
    AudioPlayback() = delete;

    // Owner-thread-only facade. A scene captures its owner thread during construction;
    // debug builds assert every register, lifecycle and control call stays there.

    static void RegisterBackend(kb::scene::Scene& scene, IAudioPlaybackBackend& backend);
    static void UnregisterBackend(kb::scene::Scene& scene, IAudioPlaybackBackend& backend) noexcept;
    [[nodiscard]] static bool HasBackend(kb::scene::Scene& scene) noexcept;
    [[nodiscard]] static AudioPlayResult PlayOneShot(kb::scene::Scene& scene, const AudioPlayDesc& desc);
    static void StopAll(kb::scene::Scene& scene) noexcept;
    [[nodiscard]] static AudioSourceControlResult PlaySource(kb::scene::Scene& scene, kb::scene::SceneEntity entity);
    [[nodiscard]] static AudioSourceControlResult PauseSource(kb::scene::Scene& scene, kb::scene::SceneEntity entity);
    [[nodiscard]] static AudioSourceControlResult ResumeSource(kb::scene::Scene& scene, kb::scene::SceneEntity entity);
    [[nodiscard]] static AudioSourceControlResult StopSource(kb::scene::Scene& scene, kb::scene::SceneEntity entity);
    [[nodiscard]] static AudioSourceControlResult IsSourcePlaying(kb::scene::Scene& scene, kb::scene::SceneEntity entity);
    [[nodiscard]] static AudioDeviceStatus DeviceStatus(kb::scene::Scene& scene) noexcept;
    [[nodiscard]] static AudioDeviceStatus Reinitialize(kb::scene::Scene& scene) noexcept;
    // LIB-148: per-voice facade - false when no backend is registered or the backend
    // reports the voice dead (see IAudioPlaybackBackend's own contract above).
    [[nodiscard]] static bool StopVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept;
    [[nodiscard]] static bool PauseVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept;
    [[nodiscard]] static bool ResumeVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept;
    [[nodiscard]] static bool SeekVoice(kb::scene::Scene& scene, std::uint64_t voiceId, float positionSeconds) noexcept;
    [[nodiscard]] static bool SetVoiceVolume(kb::scene::Scene& scene, std::uint64_t voiceId, float volume) noexcept;
    [[nodiscard]] static bool SetVoiceMute(kb::scene::Scene& scene, std::uint64_t voiceId, bool mute) noexcept;
    [[nodiscard]] static bool SetVoicePan(kb::scene::Scene& scene, std::uint64_t voiceId, float pan) noexcept;
    [[nodiscard]] static bool SetVoicePitch(kb::scene::Scene& scene, std::uint64_t voiceId, float pitch) noexcept;
    [[nodiscard]] static bool SetVoiceLoop(kb::scene::Scene& scene, std::uint64_t voiceId, bool loop) noexcept;
    [[nodiscard]] static bool IsVoicePlaying(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept;
    // LIB-152: see IAudioPlaybackBackend's contracts above; -1 / false without a backend.
    [[nodiscard]] static float VoicePlaybackSeconds(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept;
    [[nodiscard]] static bool AddVoiceMarker(kb::scene::Scene& scene, std::uint64_t voiceId, std::string_view marker, float positionSeconds, kb::scene::SceneEntity target);
    // LIB-152: the backend->script event channel (mirror of PhysicsBackend's collision
    // queue): the audio backend queues fired markers here each tick, and
    // ScriptRuntimeSceneSystem drains them into ENTITY-LOCAL "OnAudioMarker" events.
    static void QueueMarkerEvent(kb::scene::Scene& scene, PendingAudioMarkerEvent event);
    [[nodiscard]] static std::vector<PendingAudioMarkerEvent> DrainPendingMarkerEvents(kb::scene::Scene& scene);
};

} // namespace kb::audio
