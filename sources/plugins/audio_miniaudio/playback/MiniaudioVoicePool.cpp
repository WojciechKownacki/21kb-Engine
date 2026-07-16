#include "playback/MiniaudioVoicePool.hpp"

#include "assets/MiniaudioClipResolver.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAudioOcclusionAccess.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "runtime/MiniaudioSound.hpp"
#include "scene/MiniaudioOcclusionSampler.hpp"

#include <algorithm>
#include <filesystem>

namespace kb::audio_miniaudio {

kb::audio::AudioPlayResult MiniaudioVoicePool::PlayOneShot(
    ma_engine& engine,
    kb::scene::Scene& scene,
    const kb::audio::AudioPlayDesc& desc,
    const MiniaudioClipResolver& clipResolver,
    ma_sound_group* group) {
    const std::filesystem::path path = clipResolver.Resolve(scene, desc.clipAssetId);
    if (path.empty()) {
        return kb::audio::AudioPlayResult{ .started = false, .voiceId = 0U, .error = "audio clip file could not be resolved" };
    }

    PruneVoicesForClip(desc.clipAssetId, desc.priority);
    if (!PruneVoiceCapacity(desc.priority)) {
        return kb::audio::AudioPlayResult{ .started = false, .voiceId = 0U, .error = "audio one-shot pool is full of higher-priority voices" };
    }

    auto sound = std::make_unique<MiniaudioSound>();
    if (sound->InitializeFromFile(engine, path, desc.spatial, group) != MA_SUCCESS) {
        return kb::audio::AudioPlayResult{ .started = false, .voiceId = 0U, .error = "miniaudio sound could not be created" };
    }

    sound->Apply(MiniaudioSoundSettings{
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
        .position = desc.position,
    });

    if (sound->Start() != MA_SUCCESS) {
        return kb::audio::AudioPlayResult{ .started = false, .voiceId = 0U, .error = "miniaudio sound could not be started" };
    }

    const std::uint64_t voiceId = AllocateVoiceId();
    voices_.push_back(VoiceRecord{
        .voiceId = voiceId,
        .clipAssetId = desc.clipAssetId,
        .looping = desc.loop,
        .priority = desc.priority,
        .ownerEntityId = desc.ownerEntityId,
        .baseVolume = desc.volume,
        .muted = desc.mute,
        .spatial = desc.spatial,
        .sound = std::move(sound),
    });
    return kb::audio::AudioPlayResult{ .started = true, .voiceId = voiceId, .error = {} };
}

void MiniaudioVoicePool::SyncAttachedVoices(
    kb::scene::Scene& scene,
    MiniaudioOcclusionSampler* occlusionSampler,
    const kb::scene::Vec3& listenerPosition) {
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
        if (iter->sound != nullptr) {
            const kb::scene::Vec3 position = scene.Transforms().Get(owner).worldPosition;
            iter->sound->SetPosition(position);
            // LIB-151: budget-capped occlusion for spatial attached voices, keyed by
            // voiceId; the owner's own collider never occludes its voice.
            if (occlusionSampler != nullptr && iter->spatial) {
                const float scale = occlusionSampler->Sample(
                    scene,
                    kb::scene::SceneAudioOcclusionAccess::Settings(scene),
                    iter->voiceId,
                    listenerPosition,
                    position,
                    iter->ownerEntityId);
                iter->sound->SetVolume(iter->muted ? 0.0F : iter->baseVolume * scale);
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
    if (voice == nullptr || voice->sound == nullptr) {
        return false;
    }
    voice->sound->Stop();
    voice->paused = true;
    return true;
}

bool MiniaudioVoicePool::ResumeVoice(std::uint64_t voiceId) noexcept {
    VoiceRecord* voice = FindVoice(voiceId);
    if (voice == nullptr || voice->sound == nullptr) {
        return false;
    }
    voice->paused = false;
    return voice->sound->Start() == MA_SUCCESS;
}

bool MiniaudioVoicePool::SeekVoice(std::uint64_t voiceId, float positionSeconds) noexcept {
    VoiceRecord* voice = FindVoice(voiceId);
    return voice != nullptr && voice->sound != nullptr && voice->sound->SeekSeconds(positionSeconds) == MA_SUCCESS;
}

bool MiniaudioVoicePool::SetVoiceVolume(std::uint64_t voiceId, float volume) noexcept {
    VoiceRecord* voice = FindVoice(voiceId);
    if (voice == nullptr || voice->sound == nullptr) {
        return false;
    }
    voice->baseVolume = volume;
    voice->muted = false;
    voice->sound->SetVolume(volume);
    return true;
}

bool MiniaudioVoicePool::SetVoicePitch(std::uint64_t voiceId, float pitch) noexcept {
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
    return voice != nullptr && voice->sound != nullptr && !voice->paused && voice->sound->IsPlaying();
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

void MiniaudioVoicePool::PruneVoicesForClip(std::uint64_t clipAssetId, std::uint8_t incomingPriority) noexcept {
    // The per-clip cap stays a silent CLAMP (the newest request always wins its slot -
    // the pre-LIB-148 semantics); incomingPriority only shapes WHICH victim falls.
    static_cast<void>(incomingPriority);
    std::size_t count = 0U;
    for (const VoiceRecord& voice : voices_) {
        if (voice.clipAssetId == clipAssetId) {
            ++count;
        }
    }

    while (count >= kMaxOneShotVoicesPerClip) {
        // LIB-148: steal the lowest-priority voice of this clip (ties: oldest, list order
        // is age order). Per-clip stealing keeps the historical clamp semantics - the
        // newest request wins its slot even at equal priority, mirroring the pre-LIB-148
        // oldest-first behavior - but never silences a strictly higher-priority voice
        // when a lower one is available.
        auto victim = voices_.end();
        for (auto iter = voices_.begin(); iter != voices_.end(); ++iter) {
            if (iter->clipAssetId != clipAssetId) {
                continue;
            }
            if (victim == voices_.end() || iter->priority < victim->priority) {
                victim = iter;
            }
        }
        if (victim == voices_.end()) {
            return;
        }
        voices_.erase(victim);
        --count;
    }
}

bool MiniaudioVoicePool::PruneVoiceCapacity(std::uint8_t incomingPriority) noexcept {
    while (voices_.size() >= kMaxOneShotVoices) {
        auto victim = voices_.end();
        for (auto iter = voices_.begin(); iter != voices_.end(); ++iter) {
            if (victim == voices_.end() || iter->priority < victim->priority) {
                victim = iter;
            }
        }
        if (victim == voices_.end()) {
            return false;
        }
        if (victim->priority > incomingPriority) {
            // Every live voice outranks the incoming request - refuse it honestly instead
            // of stealing a higher-priority voice.
            return false;
        }
        voices_.erase(victim);
    }
    return true;
}

} // namespace kb::audio_miniaudio
