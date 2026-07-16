#pragma once

#include "assets/MiniaudioClipResolver.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "playback/MiniaudioVoicePool.hpp"
#include "runtime/MiniaudioEngine.hpp"
#include "scene/MiniaudioBusRegistry.hpp"
#include "scene/MiniaudioListenerSynchronizer.hpp"
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

private:
    MiniaudioEngine engine_;
    MiniaudioClipResolver clipResolver_;
    MiniaudioListenerSynchronizer listenerSynchronizer_;
    MiniaudioBusRegistry busRegistry_;
    MiniaudioSourceRegistry sourceRegistry_;
    MiniaudioVoicePool voicePool_;
};

} // namespace kb::audio_miniaudio
