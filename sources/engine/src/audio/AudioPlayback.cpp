#include "engine/audio/AudioPlayback.hpp"

#include "engine/scene/Scene.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

namespace kb::audio {
namespace {

[[nodiscard]] IAudioPlaybackBackend* FindBackend(kb::scene::Scene& scene) noexcept {
    return kb::scene::SceneAccess::State(scene).audioPlaybackBackend;
}

} // namespace

void AudioPlayback::RegisterBackend(kb::scene::Scene& scene, IAudioPlaybackBackend& backend) {
    kb::scene::SceneAccess::State(scene).audioPlaybackBackend = &backend;
}

void AudioPlayback::UnregisterBackend(kb::scene::Scene& scene, IAudioPlaybackBackend& backend) noexcept {
    kb::scene::SceneState& state = kb::scene::SceneAccess::State(scene);
    if (state.audioPlaybackBackend == &backend) {
        state.audioPlaybackBackend = nullptr;
    }
}

bool AudioPlayback::HasBackend(kb::scene::Scene& scene) noexcept {
    return FindBackend(scene) != nullptr;
}

AudioPlayResult AudioPlayback::PlayOneShot(kb::scene::Scene& scene, const AudioPlayDesc& desc) {
    IAudioPlaybackBackend* backend = FindBackend(scene);
    if (backend == nullptr) {
        return AudioPlayResult{ .started = false, .voiceId = 0U, .error = "audio playback backend is not active" };
    }
    return backend->PlayOneShot(scene, desc);
}

void AudioPlayback::StopAll(kb::scene::Scene& scene) noexcept {
    IAudioPlaybackBackend* backend = FindBackend(scene);
    if (backend != nullptr) {
        backend->StopAll(scene);
    }
}

} // namespace kb::audio
