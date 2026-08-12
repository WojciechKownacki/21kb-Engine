#pragma once

#include "assets/MiniaudioClipResolver.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "playback/MiniaudioVoicePool.hpp"
#include "runtime/MiniaudioEngine.hpp"
#include "scene/MiniaudioBusRegistry.hpp"
#include "scene/MiniaudioListenerSynchronizer.hpp"
#include "scene/MiniaudioOcclusionSampler.hpp"
#include "scene/MiniaudioSourceRegistry.hpp"

#include <cstddef>

namespace kb::scene {

class SceneSystemContext;

} // namespace kb::scene

namespace kb::audio_miniaudio {

class MiniaudioPlaybackBackend final : public kb::audio::IAudioPlaybackBackend {
public:
    ~MiniaudioPlaybackBackend() override;

    void OnCreate();
    void OnUpdate(kb::scene::SceneSystemContext& context);
    void Shutdown() noexcept;

    [[nodiscard]] kb::audio::AudioPlayResult PlayOneShot(kb::scene::Scene& scene, const kb::audio::AudioPlayDesc& desc) override;
    void StopAll(kb::scene::Scene& scene) noexcept override;
    [[nodiscard]] kb::audio::AudioSourceControlResult PlaySource(kb::scene::Scene& scene, kb::scene::SceneEntity entity) override;
    [[nodiscard]] kb::audio::AudioSourceControlResult PauseSource(kb::scene::Scene& scene, kb::scene::SceneEntity entity) override;
    [[nodiscard]] kb::audio::AudioSourceControlResult ResumeSource(kb::scene::Scene& scene, kb::scene::SceneEntity entity) override;
    [[nodiscard]] kb::audio::AudioSourceControlResult StopSource(kb::scene::Scene& scene, kb::scene::SceneEntity entity) override;
    [[nodiscard]] kb::audio::AudioSourceControlResult IsSourcePlaying(kb::scene::Scene& scene, kb::scene::SceneEntity entity) override;
    [[nodiscard]] kb::audio::AudioDeviceStatus DeviceStatus() const noexcept override;
    [[nodiscard]] kb::audio::AudioDeviceStatus Reinitialize(kb::scene::Scene& scene) noexcept override;
    // LIB-148: per-voice control forwarded to the one-shot voice pool (scene-agnostic -
    // this backend instance is already scene-bound through its owning scene system).
    [[nodiscard]] bool StopVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept override;
    [[nodiscard]] bool PauseVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept override;
    [[nodiscard]] bool ResumeVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept override;
    [[nodiscard]] bool SeekVoice(kb::scene::Scene& scene, std::uint64_t voiceId, float positionSeconds) noexcept override;
    [[nodiscard]] bool SetVoiceVolume(kb::scene::Scene& scene, std::uint64_t voiceId, float volume) noexcept override;
    [[nodiscard]] bool SetVoiceMute(kb::scene::Scene& scene, std::uint64_t voiceId, bool mute) noexcept override;
    [[nodiscard]] bool SetVoicePan(kb::scene::Scene& scene, std::uint64_t voiceId, float pan) noexcept override;
    [[nodiscard]] bool SetVoicePitch(kb::scene::Scene& scene, std::uint64_t voiceId, float pitch) noexcept override;
    [[nodiscard]] bool SetVoiceLoop(kb::scene::Scene& scene, std::uint64_t voiceId, bool loop) noexcept override;
    [[nodiscard]] bool IsVoicePlaying(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept override;
    [[nodiscard]] float VoicePlaybackSeconds(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept override;
    [[nodiscard]] bool AddVoiceMarker(kb::scene::Scene& scene, std::uint64_t voiceId, std::string_view marker, float positionSeconds, kb::scene::SceneEntity target) override;

#if defined(KB_AUDIO_MINIAUDIO_TESTING)
    struct ResourceStateForTesting {
        std::size_t sourceSounds = 0U;
        std::size_t voices = 0U;
        std::size_t buses = 0U;
    };

    [[nodiscard]] kb::audio::AudioDeviceStatus ReinitializeForTesting(kb::scene::Scene& scene, bool forceNoDevice) noexcept;
    [[nodiscard]] kb::audio::AudioPlayResult PlayOneShotForTesting(kb::scene::Scene& scene, const kb::audio::AudioPlayDesc& desc);
    [[nodiscard]] kb::audio::AudioSourceControlResult PlaySourceForTesting(kb::scene::Scene& scene, kb::scene::SceneEntity entity);
    [[nodiscard]] ResourceStateForTesting ResourcesForTesting() const noexcept;
    [[nodiscard]] bool StopPlaybackDeviceForTesting() noexcept;
#endif

private:
    void SyncRouting(kb::scene::Scene& scene, bool routingAvailable);
    [[nodiscard]] kb::audio::AudioPlayResult PlayOneShotInternal(kb::scene::Scene& scene, const kb::audio::AudioPlayDesc& desc, bool playbackAvailable);
    [[nodiscard]] kb::audio::AudioSourceControlResult PlaySourceInternal(kb::scene::Scene& scene, kb::scene::SceneEntity entity, bool playbackAvailable);
    [[nodiscard]] kb::audio::AudioDeviceStatus ReinitializeInternal(kb::scene::Scene& scene, bool forceNoDevice) noexcept;

    MiniaudioEngine engine_;
    MiniaudioClipResolver clipResolver_;
    MiniaudioListenerSynchronizer listenerSynchronizer_;
    MiniaudioBusRegistry busRegistry_;
    MiniaudioOcclusionSampler occlusionSampler_;
    MiniaudioSourceRegistry sourceRegistry_;
    MiniaudioVoicePool voicePool_;
};

} // namespace kb::audio_miniaudio
