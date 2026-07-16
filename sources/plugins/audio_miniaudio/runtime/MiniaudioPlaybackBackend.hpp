#pragma once

#include "assets/MiniaudioClipResolver.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "playback/MiniaudioVoicePool.hpp"
#include "runtime/MiniaudioEngine.hpp"
#include "scene/MiniaudioBusRegistry.hpp"
#include "scene/MiniaudioListenerSynchronizer.hpp"
#include "scene/MiniaudioOcclusionSampler.hpp"
#include "scene/MiniaudioSourceRegistry.hpp"

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
    // LIB-148: per-voice control forwarded to the one-shot voice pool (scene-agnostic -
    // this backend instance is already scene-bound through its owning scene system).
    [[nodiscard]] bool StopVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept override;
    [[nodiscard]] bool PauseVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept override;
    [[nodiscard]] bool ResumeVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept override;
    [[nodiscard]] bool SeekVoice(kb::scene::Scene& scene, std::uint64_t voiceId, float positionSeconds) noexcept override;
    [[nodiscard]] bool SetVoiceVolume(kb::scene::Scene& scene, std::uint64_t voiceId, float volume) noexcept override;
    [[nodiscard]] bool SetVoicePitch(kb::scene::Scene& scene, std::uint64_t voiceId, float pitch) noexcept override;
    [[nodiscard]] bool SetVoiceLoop(kb::scene::Scene& scene, std::uint64_t voiceId, bool loop) noexcept override;
    [[nodiscard]] bool IsVoicePlaying(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept override;
    [[nodiscard]] float VoicePlaybackSeconds(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept override;
    [[nodiscard]] bool AddVoiceMarker(kb::scene::Scene& scene, std::uint64_t voiceId, std::string_view marker, float positionSeconds, kb::scene::SceneEntity target) override;

private:
    MiniaudioEngine engine_;
    MiniaudioClipResolver clipResolver_;
    MiniaudioListenerSynchronizer listenerSynchronizer_;
    MiniaudioBusRegistry busRegistry_;
    MiniaudioOcclusionSampler occlusionSampler_;
    MiniaudioSourceRegistry sourceRegistry_;
    MiniaudioVoicePool voicePool_;
};

} // namespace kb::audio_miniaudio
