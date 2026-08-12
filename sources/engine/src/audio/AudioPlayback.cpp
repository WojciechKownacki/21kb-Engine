#include "engine/audio/AudioPlayback.hpp"

#include "engine/audio/AudioRoutingContract.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <utility>
#include <cmath>

namespace kb::audio {
namespace {

[[nodiscard]] IAudioPlaybackBackend* FindBackend(kb::scene::Scene& scene) noexcept {
    return kb::scene::SceneAccess::State(scene).audioPlaybackBackend;
}

[[nodiscard]] bool IsFinite(kb::scene::Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

AudioPlayDescValidationStatus ValidateAudioPlayDesc(
    const kb::scene::Scene& scene, const AudioPlayDesc& desc) noexcept {
    if (desc.clipAssetId == 0U) {
        return AudioPlayDescValidationStatus::InvalidClip;
    }
    if ((!desc.outputBus.empty() && !IsAudioMixerNameTokenValid(desc.outputBus))
        || !std::isfinite(desc.volume) || desc.volume < 0.0F
        || !std::isfinite(desc.pitch) || desc.pitch < 0.01F
        || !std::isfinite(desc.pan) || desc.pan < -1.0F || desc.pan > 1.0F
        || !std::isfinite(desc.spatialBlend) || desc.spatialBlend < 0.0F || desc.spatialBlend > 1.0F
        || !IsAudioAttenuationModelValid(desc.attenuationModel)
        || !std::isfinite(desc.minDistance) || desc.minDistance < 0.01F
        || !std::isfinite(desc.maxDistance) || desc.maxDistance < desc.minDistance
        || !std::isfinite(desc.rolloff) || desc.rolloff < 0.0F
        || !std::isfinite(desc.dopplerFactor) || desc.dopplerFactor < 0.0F
        || !IsFinite(desc.position) || !IsFinite(desc.velocity)) {
        return AudioPlayDescValidationStatus::InvalidSettings;
    }
    if (desc.ownerEntityId != 0U) {
        const kb::scene::SceneEntity owner{ desc.ownerEntityId };
        if (!scene.Entities().IsAlive(owner) || !scene.Entities().IsActive(owner)
            || scene.Transforms().TryGet(owner) == nullptr) {
            return AudioPlayDescValidationStatus::InvalidOwner;
        }
    }
    return AudioPlayDescValidationStatus::Valid;
}

AudioPlayResult AudioPlayDescValidationResult(AudioPlayDescValidationStatus status) {
    switch (status) {
    case AudioPlayDescValidationStatus::InvalidClip:
        return { .started = false, .voiceId = 0U, .error = "audio clip id is invalid" };
    case AudioPlayDescValidationStatus::InvalidOwner:
        return { .started = false, .voiceId = 0U, .error = "audio voice owner is unavailable" };
    case AudioPlayDescValidationStatus::InvalidSettings:
        return { .started = false, .voiceId = 0U, .error = "audio playback settings are invalid" };
    case AudioPlayDescValidationStatus::Valid:
        break;
    }
    return {};
}

bool IsAudioVoiceMarkerRequestValid(
    const kb::scene::Scene& scene,
    std::string_view marker,
    float positionSeconds,
    kb::scene::SceneEntity target) noexcept {
    return IsAudioVoiceMarkerNameValid(marker)
        && IsAudioVoiceSeekPositionValid(positionSeconds)
        && target.IsValid()
        && scene.Entities().IsAlive(target);
}

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
    const AudioPlayDescValidationStatus validation = ValidateAudioPlayDesc(scene, desc);
    if (validation != AudioPlayDescValidationStatus::Valid) {
        return AudioPlayDescValidationResult(validation);
    }
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

AudioSourceControlResult AudioPlayback::PlaySource(kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    IAudioPlaybackBackend* backend = FindBackend(scene);
    return backend == nullptr ? AudioSourceControlResult{} : backend->PlaySource(scene, entity);
}

AudioSourceControlResult AudioPlayback::PauseSource(kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    IAudioPlaybackBackend* backend = FindBackend(scene);
    return backend == nullptr ? AudioSourceControlResult{} : backend->PauseSource(scene, entity);
}

AudioSourceControlResult AudioPlayback::ResumeSource(kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    IAudioPlaybackBackend* backend = FindBackend(scene);
    return backend == nullptr ? AudioSourceControlResult{} : backend->ResumeSource(scene, entity);
}

AudioSourceControlResult AudioPlayback::StopSource(kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    IAudioPlaybackBackend* backend = FindBackend(scene);
    return backend == nullptr ? AudioSourceControlResult{} : backend->StopSource(scene, entity);
}

AudioSourceControlResult AudioPlayback::IsSourcePlaying(kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    IAudioPlaybackBackend* backend = FindBackend(scene);
    return backend == nullptr ? AudioSourceControlResult{} : backend->IsSourcePlaying(scene, entity);
}

AudioDeviceStatus AudioPlayback::DeviceStatus(kb::scene::Scene& scene) noexcept {
    IAudioPlaybackBackend* backend = FindBackend(scene);
    return backend == nullptr ? AudioDeviceStatus::BackendUnavailable : backend->DeviceStatus();
}

AudioDeviceStatus AudioPlayback::Reinitialize(kb::scene::Scene& scene) noexcept {
    IAudioPlaybackBackend* backend = FindBackend(scene);
    return backend == nullptr ? AudioDeviceStatus::BackendUnavailable : backend->Reinitialize(scene);
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
    if (!IsAudioVoiceSeekPositionValid(positionSeconds)) {
        return false;
    }
    IAudioPlaybackBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->SeekVoice(scene, voiceId, positionSeconds);
}

bool AudioPlayback::SetVoiceVolume(kb::scene::Scene& scene, std::uint64_t voiceId, float volume) noexcept {
    if (!IsAudioVoiceVolumeValid(volume)) {
        return false;
    }
    IAudioPlaybackBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->SetVoiceVolume(scene, voiceId, volume);
}

bool AudioPlayback::SetVoiceMute(kb::scene::Scene& scene, std::uint64_t voiceId, bool mute) noexcept {
    IAudioPlaybackBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->SetVoiceMute(scene, voiceId, mute);
}

bool AudioPlayback::SetVoicePan(kb::scene::Scene& scene, std::uint64_t voiceId, float pan) noexcept {
    if (!IsAudioVoicePanValid(pan)) {
        return false;
    }
    IAudioPlaybackBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->SetVoicePan(scene, voiceId, pan);
}

bool AudioPlayback::SetVoicePitch(kb::scene::Scene& scene, std::uint64_t voiceId, float pitch) noexcept {
    if (!IsAudioVoicePitchValid(pitch)) {
        return false;
    }
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
    if (!IsAudioVoiceMarkerRequestValid(scene, marker, positionSeconds, target)) {
        return false;
    }
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
