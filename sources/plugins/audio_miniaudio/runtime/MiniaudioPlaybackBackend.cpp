#include "runtime/MiniaudioPlaybackBackend.hpp"

#include "engine/scene/SceneSystemContext.hpp"

namespace kb::audio_miniaudio {

MiniaudioPlaybackBackend::~MiniaudioPlaybackBackend() {
    Shutdown();
}

void MiniaudioPlaybackBackend::OnCreate() {
    engine_.Initialize();
}

void MiniaudioPlaybackBackend::OnUpdate(kb::scene::SceneSystemContext& context) {
    if (!engine_.IsInitialized()) {
        return;
    }

    listenerSynchronizer_.Sync(engine_.Native(), context);
    sourceRegistry_.Sync(engine_.Native(), context, clipResolver_, engine_.IsPlaybackAvailable());
    voicePool_.RemoveFinishedVoices();
}

void MiniaudioPlaybackBackend::Shutdown() noexcept {
    sourceRegistry_.StopAll();
    voicePool_.StopAll();
    engine_.Shutdown();
}

kb::audio::AudioPlayResult MiniaudioPlaybackBackend::PlayOneShot(kb::scene::Scene& scene, const kb::audio::AudioPlayDesc& desc) {
    if (!engine_.IsInitialized()) {
        return kb::audio::AudioPlayResult{ .started = false, .voiceId = 0U, .error = "miniaudio engine is not initialized" };
    }
    if (!engine_.IsPlaybackAvailable()) {
        return kb::audio::AudioPlayResult{ .started = false, .voiceId = 0U, .error = "miniaudio playback device is not available" };
    }
    if (desc.clipAssetId == 0U) {
        return kb::audio::AudioPlayResult{ .started = false, .voiceId = 0U, .error = "audio clip id is invalid" };
    }

    return voicePool_.PlayOneShot(engine_.Native(), scene, desc, clipResolver_);
}

void MiniaudioPlaybackBackend::StopAll(kb::scene::Scene& scene) noexcept {
    static_cast<void>(scene);
    sourceRegistry_.StopAll();
    voicePool_.StopAll();
}

} // namespace kb::audio_miniaudio
