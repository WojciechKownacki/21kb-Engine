#include "playback/MiniaudioVoicePool.hpp"

#include "assets/MiniaudioClipResolver.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAudioOcclusionAccess.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "runtime/MiniaudioSound.hpp"
#include "scene/MiniaudioOcclusionSampler.hpp"

#include <algorithm>
#include <cmath>

namespace kb::audio_miniaudio {
namespace {

[[nodiscard]] float FiniteOrZero(float value) noexcept {
    return std::isfinite(value) ? value : 0.0F;
}

[[nodiscard]] kb::scene::Vec3 FiniteOrZero(kb::scene::Vec3 value) noexcept {
    return { FiniteOrZero(value.x), FiniteOrZero(value.y), FiniteOrZero(value.z) };
}

} // namespace

kb::audio::AudioPlayResult MiniaudioVoicePool::PlayOneShot(
    ma_engine& engine,
    kb::scene::Scene& scene,
    const kb::audio::AudioPlayDesc& desc,
    const MiniaudioClipResolver& clipResolver,
    ma_sound_group* group) {
    const kb::audio::AudioPlayDescValidationStatus validation = kb::audio::ValidateAudioPlayDesc(scene, desc);
    if (validation != kb::audio::AudioPlayDescValidationStatus::Valid) {
        return kb::audio::AudioPlayDescValidationResult(validation);
    }
    kb::scene::Vec3 initialPosition = desc.position;
    bool attached = false;
    if (desc.ownerEntityId != 0U) {
        const kb::scene::SceneEntity owner{ desc.ownerEntityId };
        const kb::scene::TransformComponent* ownerTransform = scene.Transforms().TryGet(owner);
        if (!scene.Entities().IsAlive(owner) || !scene.Entities().IsActive(owner) || ownerTransform == nullptr) {
            return kb::audio::AudioPlayResult{ .started = false, .voiceId = 0U, .error = "audio voice owner is unavailable" };
        }
        initialPosition = FiniteOrZero(ownerTransform->worldPosition);
        attached = true;
    }
    const MiniaudioClipResolver::Resolution resolution = clipResolver.Resolve(scene, desc.clipAssetId);
    if (!resolution.Succeeded()) {
        return kb::audio::AudioPlayResult{ .started = false, .voiceId = 0U, .error = "audio clip file could not be resolved" };
    }

    Admission admission{ .clipVictim = voices_.end(), .capacityVictim = voices_.end() };
    bool clipRejected = false;
    if (!PlanAdmission(desc.clipAssetId, desc.priority, admission, clipRejected)) {
        return kb::audio::AudioPlayResult{
            .started = false,
            .voiceId = 0U,
            .error = clipRejected
                ? "audio clip pool is full of higher-priority voices"
                : "audio one-shot pool is full of higher-priority voices",
        };
    }

    auto sound = std::make_unique<MiniaudioSound>();
    if (sound->Initialize(engine, resolution.clip, desc.spatial, group) != MA_SUCCESS) {
        return kb::audio::AudioPlayResult{ .started = false, .voiceId = 0U, .error = "audio voice could not be created" };
    }

    MiniaudioSoundSettings settings{
        .volume = desc.volume,
        .pitch = desc.pitch,
        .mute = desc.mute,
        .loop = desc.loop,
        .spatial = desc.spatial,
        .pan = desc.pan,
        .spatialBlend = desc.spatialBlend,
        .attenuationModel = desc.attenuationModel,
        .minDistance = desc.minDistance,
        .maxDistance = desc.maxDistance,
        .rolloff = desc.rolloff,
        .dopplerFactor = desc.dopplerFactor,
        .position = initialPosition,
        .velocity = attached ? kb::scene::Vec3{} : desc.velocity,
    };
    MiniaudioSoundSettings candidateSettings = settings;
    candidateSettings.mute = true;
    sound->Apply(candidateSettings);

    if (sound->Start() != MA_SUCCESS) {
        return kb::audio::AudioPlayResult{ .started = false, .voiceId = 0U, .error = "audio voice could not be started" };
    }

    const std::uint64_t voiceId = AllocateVoiceId();
    std::list<VoiceRecord> committedCandidate;
    committedCandidate.push_back(VoiceRecord{
        .voiceId = voiceId,
        .clipAssetId = desc.clipAssetId,
        .looping = desc.loop,
        .priority = desc.priority,
        .ownerEntityId = desc.ownerEntityId,
        .baseVolume = MiniaudioSound::NormalizeVolume(desc.volume),
        .muted = desc.mute,
        .spatial = desc.spatial,
        .previousOwnerPosition = initialPosition,
        .hasPreviousOwnerPosition = false,
        .sound = std::move(sound),
    });
    if (admission.clipVictim != voices_.end()) {
        voices_.erase(admission.clipVictim);
    }
    if (admission.capacityVictim != voices_.end()) {
        voices_.erase(admission.capacityVictim);
    }
    voices_.splice(voices_.end(), committedCandidate);
    voices_.back().sound->Apply(settings);
    return kb::audio::AudioPlayResult{ .started = true, .voiceId = voiceId, .error = {} };
}

void MiniaudioVoicePool::SyncAttachedVoices(
    kb::scene::Scene& scene,
    MiniaudioOcclusionSampler* occlusionSampler,
    const kb::scene::Vec3& listenerPosition,
    float deltaSeconds) {
    const bool validDelta = std::isfinite(deltaSeconds) && deltaSeconds > 0.0F;
    for (auto iter = voices_.begin(); iter != voices_.end();) {
        if (iter->ownerEntityId == 0U) {
            ++iter;
            continue;
        }
        const kb::scene::SceneEntity owner{ iter->ownerEntityId };
        // The canonical owner-gone poll (ScriptEventBus/SceneTimerService convention) -
        // destroying the record stops the ma_sound and releases the source immediately,
        // looping/paused voices included.
        if (!scene.Entities().IsAlive(owner) || !scene.Entities().IsActive(owner)) {
            iter = voices_.erase(iter);
            continue;
        }
        const kb::scene::TransformComponent* ownerTransform = scene.Transforms().TryGet(owner);
        if (ownerTransform == nullptr) {
            iter = voices_.erase(iter);
            continue;
        }
        if (iter->sound != nullptr) {
            const kb::scene::Vec3 position = FiniteOrZero(ownerTransform->worldPosition);
            kb::scene::Vec3 velocity{};
            if (validDelta && iter->hasPreviousOwnerPosition) {
                velocity = kb::scene::Vec3{
                    (position.x - iter->previousOwnerPosition.x) / deltaSeconds,
                    (position.y - iter->previousOwnerPosition.y) / deltaSeconds,
                    (position.z - iter->previousOwnerPosition.z) / deltaSeconds,
                };
                velocity = FiniteOrZero(velocity);
            }
            iter->previousOwnerPosition = position;
            iter->hasPreviousOwnerPosition = true;
            iter->sound->SetPosition(position);
            iter->sound->SetVelocity(velocity);
            iter->sound->SetVolume(iter->baseVolume);
            iter->sound->SetMute(iter->muted);
            // LIB-151: budget-capped occlusion for spatial attached voices, keyed by
            // voiceId; the owner's own collider never occludes its voice.
            if (occlusionSampler != nullptr && iter->spatial) {
                const float scale = occlusionSampler->Sample(
                    scene,
                    kb::scene::SceneAudioOcclusionAccess::Settings(scene),
                    MiniaudioOcclusionKey{ MiniaudioOcclusionKeyKind::Voice, iter->voiceId },
                    listenerPosition,
                    position,
                    iter->ownerEntityId);
                iter->sound->SetVolume(iter->baseVolume * scale);
            }
        }
        ++iter;
    }
}

bool MiniaudioVoicePool::StopVoice(std::uint64_t voiceId) noexcept {
    for (auto iter = voices_.begin(); iter != voices_.end(); ++iter) {
        if (iter->voiceId == voiceId) {
            voices_.erase(iter);
            return true;
        }
    }
    return false;
}

bool MiniaudioVoicePool::PauseVoice(std::uint64_t voiceId) noexcept {
    VoiceRecord* voice = FindVoice(voiceId);
    if (voice == nullptr || voice->sound == nullptr || voice->paused || voice->sound->AtEnd() || !voice->sound->IsPlaying()) {
        return false;
    }
    voice->sound->Stop();
    voice->paused = true;
    return true;
}

bool MiniaudioVoicePool::ResumeVoice(std::uint64_t voiceId) noexcept {
    VoiceRecord* voice = FindVoice(voiceId);
    if (voice == nullptr || voice->sound == nullptr || !voice->paused || voice->sound->AtEnd()) {
        return false;
    }
    if (voice->sound->Start() != MA_SUCCESS) {
        return false;
    }
    voice->paused = false;
    return true;
}

bool MiniaudioVoicePool::SeekVoice(std::uint64_t voiceId, float positionSeconds) noexcept {
    if (!kb::audio::IsAudioVoiceSeekPositionValid(positionSeconds)) {
        return false;
    }
    VoiceRecord* voice = FindVoice(voiceId);
    return voice != nullptr && voice->sound != nullptr && voice->sound->SeekSeconds(positionSeconds) == MA_SUCCESS;
}

bool MiniaudioVoicePool::SetVoiceVolume(std::uint64_t voiceId, float volume) noexcept {
    if (!kb::audio::IsAudioVoiceVolumeValid(volume)) {
        return false;
    }
    VoiceRecord* voice = FindVoice(voiceId);
    if (voice == nullptr || voice->sound == nullptr) {
        return false;
    }
    voice->baseVolume = MiniaudioSound::NormalizeVolume(volume);
    voice->sound->SetVolume(voice->baseVolume);
    return true;
}

bool MiniaudioVoicePool::SetVoiceMute(std::uint64_t voiceId, bool mute) noexcept {
    VoiceRecord* voice = FindVoice(voiceId);
    if (voice == nullptr || voice->sound == nullptr) {
        return false;
    }
    voice->muted = mute;
    voice->sound->SetMute(mute);
    return true;
}

bool MiniaudioVoicePool::SetVoicePan(std::uint64_t voiceId, float pan) noexcept {
    if (!kb::audio::IsAudioVoicePanValid(pan)) {
        return false;
    }
    VoiceRecord* voice = FindVoice(voiceId);
    if (voice == nullptr || voice->sound == nullptr) {
        return false;
    }
    voice->sound->SetPan(pan);
    return true;
}

bool MiniaudioVoicePool::SetVoicePitch(std::uint64_t voiceId, float pitch) noexcept {
    if (!kb::audio::IsAudioVoicePitchValid(pitch)) {
        return false;
    }
    VoiceRecord* voice = FindVoice(voiceId);
    if (voice == nullptr || voice->sound == nullptr) {
        return false;
    }
    voice->sound->SetPitch(pitch);
    return true;
}

bool MiniaudioVoicePool::SetVoiceLoop(std::uint64_t voiceId, bool loop) noexcept {
    VoiceRecord* voice = FindVoice(voiceId);
    if (voice == nullptr || voice->sound == nullptr) {
        return false;
    }
    voice->sound->SetLooping(loop);
    voice->looping = loop;
    return true;
}

bool MiniaudioVoicePool::IsVoicePlaying(std::uint64_t voiceId) noexcept {
    VoiceRecord* voice = FindVoice(voiceId);
    return voice != nullptr && voice->sound != nullptr && !voice->paused
        && !voice->sound->AtEnd() && voice->sound->IsPlaying();
}

float MiniaudioVoicePool::VoicePlaybackSeconds(std::uint64_t voiceId) noexcept {
    VoiceRecord* voice = FindVoice(voiceId);
    return voice == nullptr || voice->sound == nullptr ? -1.0F : voice->sound->PlaybackSeconds();
}

bool MiniaudioVoicePool::AddVoiceMarker(kb::scene::Scene& scene, std::uint64_t voiceId, std::string_view marker, float positionSeconds, kb::scene::SceneEntity target) {
    if (!kb::audio::IsAudioVoiceMarkerRequestValid(scene, marker, positionSeconds, target)) {
        return false;
    }
    VoiceRecord* voice = FindVoice(voiceId);
    if (voice == nullptr || voice->sound == nullptr) {
        return false;
    }
    voice->markers.push_back(VoiceMarker{
        .name = std::string{ marker },
        .positionSeconds = positionSeconds,
        .target = target,
        .fired = false,
    });
    return true;
}

void MiniaudioVoicePool::DispatchMarkers(kb::scene::Scene& scene) {
    for (VoiceRecord& voice : voices_) {
        if (voice.markers.empty() || voice.sound == nullptr || voice.paused) {
            continue;
        }
        const float position = voice.sound->PlaybackSeconds();
        if (position < 0.0F) {
            continue;
        }
        for (VoiceMarker& marker : voice.markers) {
            if (marker.fired || position < marker.positionSeconds) {
                continue;
            }
            marker.fired = true;
            kb::audio::AudioPlayback::QueueMarkerEvent(scene, kb::audio::PendingAudioMarkerEvent{
                                                                  .target = marker.target,
                                                                  .voiceId = voice.voiceId,
                                                                  .marker = marker.name,
                                                                  .positionSeconds = position,
                                                              });
        }
    }
}

MiniaudioVoicePool::VoiceRecord* MiniaudioVoicePool::FindVoice(std::uint64_t voiceId) noexcept {
    if (voiceId == 0U) {
        return nullptr;
    }
    for (VoiceRecord& voice : voices_) {
        if (voice.voiceId == voiceId) {
            return &voice;
        }
    }
    return nullptr;
}

void MiniaudioVoicePool::RemoveFinishedVoices() noexcept {
    for (auto iter = voices_.begin(); iter != voices_.end();) {
        // LIB-148: a paused voice is neither playing nor at_end - it must survive until
        // Resume or an explicit Stop, never be reclaimed as "finished".
        if (iter->looping || iter->paused || iter->sound == nullptr || !iter->sound->AtEnd()) {
            ++iter;
            continue;
        }
        iter = voices_.erase(iter);
    }
}

void MiniaudioVoicePool::StopAll() noexcept {
    voices_.clear();
}

std::uint64_t MiniaudioVoicePool::AllocateVoiceId() noexcept {
    const std::uint64_t voiceId = nextVoiceId_++;
    if (nextVoiceId_ == 0U) {
        nextVoiceId_ = 1U;
    }
    return voiceId;
}

bool MiniaudioVoicePool::PlanAdmission(
    std::uint64_t clipAssetId,
    std::uint8_t incomingPriority,
    Admission& admission,
    bool& clipRejected) noexcept {
    admission = Admission{ .clipVictim = voices_.end(), .capacityVictim = voices_.end() };
    clipRejected = false;

    std::size_t count = 0U;
    for (auto iterator = voices_.begin(); iterator != voices_.end(); ++iterator) {
        if (iterator->clipAssetId == clipAssetId) {
            ++count;
            if (admission.clipVictim == voices_.end() || iterator->priority < admission.clipVictim->priority) {
                admission.clipVictim = iterator;
            }
        }
    }

    if (count >= kMaxOneShotVoicesPerClip) {
        if (admission.clipVictim == voices_.end() || admission.clipVictim->priority > incomingPriority) {
            clipRejected = true;
            return false;
        }
    } else {
        admission.clipVictim = voices_.end();
    }

    const std::size_t sizeAfterClipCommit = voices_.size() - (admission.clipVictim == voices_.end() ? 0U : 1U);
    if (sizeAfterClipCommit >= kMaxOneShotVoices) {
        for (auto iterator = voices_.begin(); iterator != voices_.end(); ++iterator) {
            if (iterator == admission.clipVictim) {
                continue;
            }
            if (admission.capacityVictim == voices_.end() || iterator->priority < admission.capacityVictim->priority) {
                admission.capacityVictim = iterator;
            }
        }
        if (admission.capacityVictim == voices_.end() || admission.capacityVictim->priority > incomingPriority) {
            return false;
        }
    }
    return true;
}

} // namespace kb::audio_miniaudio
