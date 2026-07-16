#include "engine/audio/AudioPlayback.hpp"

#include "engine/scene/Scene.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <utility>

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

bool AudioPlayback::StopVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept {
    IAudioPlaybackBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->StopVoice(scene, voiceId);
}

bool AudioPlayback::PauseVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept {
    IAudioPlaybackBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->PauseVoice(scene, voiceId);
}

bool AudioPlayback::ResumeVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept {
    IAudioPlaybackBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->ResumeVoice(scene, voiceId);
}

bool AudioPlayback::SeekVoice(kb::scene::Scene& scene, std::uint64_t voiceId, float positionSeconds) noexcept {
    IAudioPlaybackBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->SeekVoice(scene, voiceId, positionSeconds);
}

bool AudioPlayback::SetVoiceVolume(kb::scene::Scene& scene, std::uint64_t voiceId, float volume) noexcept {
    IAudioPlaybackBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->SetVoiceVolume(scene, voiceId, volume);
}

bool AudioPlayback::SetVoicePitch(kb::scene::Scene& scene, std::uint64_t voiceId, float pitch) noexcept {
    IAudioPlaybackBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->SetVoicePitch(scene, voiceId, pitch);
}

bool AudioPlayback::SetVoiceLoop(kb::scene::Scene& scene, std::uint64_t voiceId, bool loop) noexcept {
    IAudioPlaybackBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->SetVoiceLoop(scene, voiceId, loop);
}

bool AudioPlayback::IsVoicePlaying(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept {
    IAudioPlaybackBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->IsVoicePlaying(scene, voiceId);
}

float AudioPlayback::VoicePlaybackSeconds(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept {
    IAudioPlaybackBackend* backend = FindBackend(scene);
    return backend == nullptr ? -1.0F : backend->VoicePlaybackSeconds(scene, voiceId);
}

bool AudioPlayback::AddVoiceMarker(kb::scene::Scene& scene, std::uint64_t voiceId, std::string_view marker, float positionSeconds, kb::scene::SceneEntity target) {
    IAudioPlaybackBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->AddVoiceMarker(scene, voiceId, marker, positionSeconds, target);
}

void AudioPlayback::QueueMarkerEvent(kb::scene::Scene& scene, PendingAudioMarkerEvent event) {
    kb::scene::SceneAccess::State(scene).pendingAudioMarkerEvents.push_back(std::move(event));
}

std::vector<PendingAudioMarkerEvent> AudioPlayback::DrainPendingMarkerEvents(kb::scene::Scene& scene) {
    return std::exchange(kb::scene::SceneAccess::State(scene).pendingAudioMarkerEvents, {});
}

} // namespace kb::audio
