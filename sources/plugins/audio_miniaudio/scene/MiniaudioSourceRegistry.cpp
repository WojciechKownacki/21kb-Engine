#include "scene/MiniaudioSourceRegistry.hpp"

#include "assets/MiniaudioClipResolver.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAudioOcclusionAccess.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "runtime/MiniaudioSound.hpp"
#include "scene/MiniaudioBusRegistry.hpp"
#include "scene/MiniaudioOcclusionSampler.hpp"

#include <cmath>

namespace kb::audio_miniaudio {
namespace {

[[nodiscard]] float FiniteOrZero(float value) noexcept {
    return std::isfinite(value) ? value : 0.0F;
}

[[nodiscard]] kb::scene::Vec3 FiniteOrZero(kb::scene::Vec3 value) noexcept {
    return { FiniteOrZero(value.x), FiniteOrZero(value.y), FiniteOrZero(value.z) };
}

[[nodiscard]] MiniaudioSoundSettings ToSoundSettings(
    const kb::scene::AudioSourceComponent& source,
    const kb::scene::TransformComponent& transform,
    const kb::scene::Vec3& velocity) noexcept {
    return MiniaudioSoundSettings{
        .volume = source.volume,
        .pitch = source.pitch,
        .mute = source.mute,
        .loop = source.loop,
        .spatial = source.spatial,
        .pan = source.pan,
        .spatialBlend = source.spatialBlend,
        .attenuationModel = source.attenuationModel,
        .minDistance = source.minDistance,
        .maxDistance = source.maxDistance,
        .rolloff = source.rolloff,
        .dopplerFactor = source.dopplerFactor,
        .position = FiniteOrZero(transform.worldPosition),
        .velocity = FiniteOrZero(velocity),
    };
}

[[nodiscard]] kb::audio::AudioSourceControlStatus ToControlStatus(MiniaudioBusRegistry::RouteStatus status) noexcept {
    switch (status) {
    case MiniaudioBusRegistry::RouteStatus::MixerUnavailable:
        return kb::audio::AudioSourceControlStatus::MixerUnavailable;
    case MiniaudioBusRegistry::RouteStatus::UnknownBus:
        return kb::audio::AudioSourceControlStatus::UnknownBus;
    case MiniaudioBusRegistry::RouteStatus::InitializationFailed:
        return kb::audio::AudioSourceControlStatus::RoutingInitializationFailed;
    case MiniaudioBusRegistry::RouteStatus::Master:
    case MiniaudioBusRegistry::RouteStatus::Routed:
    default:
        return kb::audio::AudioSourceControlStatus::Success;
    }
}

} // namespace

void MiniaudioSourceRegistry::Sync(
    ma_engine& engine,
    kb::scene::SceneSystemContext& context,
    const MiniaudioClipResolver& clipResolver,
    MiniaudioBusRegistry& busRegistry,
    MiniaudioOcclusionSampler* occlusionSampler,
    const kb::scene::Vec3& listenerPosition,
    bool playbackAvailable) {
    seenEntities_.clear();
    SourceSyncContext syncContext{
        .registry = this,
        .engine = &engine,
        .scene = &context.GetScene(),
        .clipResolver = &clipResolver,
        .busRegistry = &busRegistry,
        .occlusionSampler = occlusionSampler,
        .listenerPosition = listenerPosition,
        .playbackAvailable = playbackAvailable,
        .deltaSeconds = context.DeltaSeconds(),
    };
    context.Transforms().ForEach(&SyncSourceFromTransform, &syncContext);
    RemoveUnseenSounds();
}

void MiniaudioSourceRegistry::StopAll() noexcept {
    sounds_.clear();
}

void MiniaudioSourceRegistry::ReleaseNativeResources() noexcept {
    for (auto& [_, record] : sounds_) {
        if (record.sound != nullptr && record.playbackState == SoundRecord::PlaybackState::Playing && record.sound->AtEnd()) {
            record.playbackState = SoundRecord::PlaybackState::Stopped;
            record.resumeFrame = 0U;
        }
        if (record.sound != nullptr && record.playbackState != SoundRecord::PlaybackState::Stopped) {
            record.resumeFrame = record.sound->PlaybackFrame();
        }
        record.sound.reset();
        record.hasPreviousPosition = false;
    }
}

void MiniaudioSourceRegistry::SyncSourceFromTransform(
    kb::scene::SceneEntity entity,
    const kb::scene::TransformComponent& transform,
    void* context) {
    auto* syncContext = static_cast<SourceSyncContext*>(context);
    syncContext->registry->SyncSource(
        *syncContext->engine,
        *syncContext->scene,
        entity,
        transform,
        *syncContext->clipResolver,
        *syncContext->busRegistry,
        syncContext->occlusionSampler,
        syncContext->listenerPosition,
        syncContext->playbackAvailable,
        syncContext->deltaSeconds);
}

void MiniaudioSourceRegistry::SyncSource(
    ma_engine& engine,
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    const kb::scene::TransformComponent& transform,
    const MiniaudioClipResolver& clipResolver,
    MiniaudioBusRegistry& busRegistry,
    MiniaudioOcclusionSampler* occlusionSampler,
    const kb::scene::Vec3& listenerPosition,
    bool playbackAvailable,
    float deltaSeconds) {
    if (!scene.Entities().IsActive(entity)) {
        RemoveSound(entity.Id());
        return;
    }
    const kb::scene::AudioSourceComponent* source = scene.Components().AudioSources().TryGet(entity);
    if (source == nullptr) {
        return;
    }

    seenEntities_.insert(entity.Id());
    if (!source->enabled || !playbackAvailable) {
        RemoveSound(entity.Id());
        return;
    }

    const std::filesystem::path path = clipResolver.Resolve(scene, source->clipAssetId);
    if (path.empty()) {
        RemoveSound(entity.Id());
        return;
    }

    const std::string busName{ kb::scene::AudioSourceOutputBus(*source) };
    const MiniaudioBusRegistry::Route route = busRegistry.Resolve(busName);
    if (!route.Succeeded()) {
        RemoveSound(entity.Id());
        return;
    }

    const kb::scene::Vec3 position = FiniteOrZero(transform.worldPosition);
    kb::scene::Vec3 velocity{};
    const auto existing = sounds_.find(entity.Id());
    const bool validDelta = std::isfinite(deltaSeconds) && deltaSeconds > 0.0F;
    if (existing != sounds_.end() && existing->second.hasPreviousPosition && validDelta) {
        velocity = kb::scene::Vec3{
            (position.x - existing->second.previousPosition.x) / deltaSeconds,
            (position.y - existing->second.previousPosition.y) / deltaSeconds,
            (position.z - existing->second.previousPosition.z) / deltaSeconds,
        };
        velocity = FiniteOrZero(velocity);
    }

    const SoundSignature signature{
        .clipAssetId = source->clipAssetId,
        .path = path,
        .busName = busName,
        .busGeneration = busRegistry.Generation(),
        .spatial = source->spatial,
    };
    SoundRecord* record = EnsureSound(engine, entity.Id(), signature, *source, transform, velocity, route.group);
    if (record == nullptr || record->sound == nullptr || !record->sound->IsInitialized()) {
        return;
    }
    record->previousPosition = position;
    record->hasPreviousPosition = true;

    MiniaudioSoundSettings settings = ToSoundSettings(*source, transform, velocity);
    if (occlusionSampler != nullptr && source->spatial) {
        settings.volume *= occlusionSampler->Sample(
            scene,
            kb::scene::SceneAudioOcclusionAccess::Settings(scene),
            MiniaudioOcclusionKey{ MiniaudioOcclusionKeyKind::Source, entity.Id() },
            listenerPosition,
            position,
            entity.Id());
    }
    record->sound->Apply(settings);
}

MiniaudioSourceRegistry::SoundRecord* MiniaudioSourceRegistry::EnsureSound(
    ma_engine& engine,
    std::uint64_t entityId,
    const SoundSignature& signature,
    const kb::scene::AudioSourceComponent& source,
    const kb::scene::TransformComponent& transform,
    const kb::scene::Vec3& velocity,
    ma_sound_group* group) {
    auto iterator = sounds_.find(entityId);
    if (iterator != sounds_.end() && iterator->second.signature == signature && iterator->second.sound != nullptr) {
        return &iterator->second;
    }

    const bool isNew = iterator == sounds_.end();
    SoundRecord::PlaybackState playbackState = source.autoplay
        ? SoundRecord::PlaybackState::Playing
        : SoundRecord::PlaybackState::Stopped;
    ma_uint64 playbackFrame = 0U;
    kb::scene::Vec3 previousPosition{};
    bool hasPreviousPosition = false;
    if (!isNew) {
        playbackState = iterator->second.playbackState;
        const bool sameClip = iterator->second.signature.clipAssetId == signature.clipAssetId && iterator->second.signature.path == signature.path;
        if (sameClip && playbackState != SoundRecord::PlaybackState::Stopped) {
            playbackFrame = iterator->second.sound != nullptr
                ? iterator->second.sound->PlaybackFrame()
                : iterator->second.resumeFrame;
        }
        previousPosition = iterator->second.previousPosition;
        hasPreviousPosition = iterator->second.hasPreviousPosition;
        sounds_.erase(iterator);
    }

    auto sound = std::make_unique<MiniaudioSound>();
    if (sound->InitializeFromFile(engine, signature.path, signature.spatial, group, playbackFrame) != MA_SUCCESS) {
        return nullptr;
    }
    sound->Apply(ToSoundSettings(source, transform, velocity));
    if (playbackState == SoundRecord::PlaybackState::Playing && sound->Start() != MA_SUCCESS) {
        return nullptr;
    }

    auto [inserted, _] = sounds_.try_emplace(entityId, SoundRecord{
        .signature = signature,
        .sound = std::move(sound),
        .playbackState = playbackState,
        .resumeFrame = playbackFrame,
        .previousPosition = previousPosition,
        .hasPreviousPosition = hasPreviousPosition,
    });
    return &inserted->second;
}

kb::audio::AudioSourceControlResult MiniaudioSourceRegistry::ValidateSource(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    bool playbackAvailable) noexcept {
    if (!entity.IsValid() || !scene.Entities().IsAlive(entity)) {
        return { .status = kb::audio::AudioSourceControlStatus::InvalidEntity };
    }
    if (!scene.Entities().IsActive(entity)) {
        return { .status = kb::audio::AudioSourceControlStatus::InactiveEntity };
    }
    const kb::scene::AudioSourceComponent* source = scene.Components().AudioSources().TryGet(entity);
    if (source == nullptr) {
        return { .status = kb::audio::AudioSourceControlStatus::MissingComponent };
    }
    if (!source->enabled) {
        return { .status = kb::audio::AudioSourceControlStatus::Disabled };
    }
    if (source->clipAssetId == 0U) {
        return { .status = kb::audio::AudioSourceControlStatus::InvalidClip };
    }
    if (!playbackAvailable) {
        return { .status = kb::audio::AudioSourceControlStatus::DeviceUnavailable };
    }
    return { .status = kb::audio::AudioSourceControlStatus::Success };
}

kb::audio::AudioSourceControlResult MiniaudioSourceRegistry::PlaySource(
    ma_engine& engine,
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    const MiniaudioClipResolver& clipResolver,
    MiniaudioBusRegistry& busRegistry,
    bool playbackAvailable) {
    const kb::audio::AudioSourceControlResult validation = ValidateSource(scene, entity, playbackAvailable);
    if (!validation.Succeeded()) {
        return validation;
    }
    const kb::scene::AudioSourceComponent& source = *scene.Components().AudioSources().TryGet(entity);
    const std::filesystem::path path = clipResolver.Resolve(scene, source.clipAssetId);
    if (path.empty()) {
        return { .status = kb::audio::AudioSourceControlStatus::ClipUnavailable };
    }
    const std::string busName{ kb::scene::AudioSourceOutputBus(source) };
    const MiniaudioBusRegistry::Route route = busRegistry.Resolve(busName);
    if (!route.Succeeded()) {
        return { .status = ToControlStatus(route.status) };
    }
    const kb::scene::TransformComponent* transform = scene.Transforms().TryGet(entity);
    if (transform == nullptr) {
        return { .status = kb::audio::AudioSourceControlStatus::InvalidEntity };
    }
    const SoundSignature signature{
        .clipAssetId = source.clipAssetId,
        .path = path,
        .busName = busName,
        .busGeneration = busRegistry.Generation(),
        .spatial = source.spatial,
    };
    SoundRecord* record = EnsureSound(engine, entity.Id(), signature, source, *transform, {}, route.group);
    if (record == nullptr || record->sound == nullptr) {
        return { .status = kb::audio::AudioSourceControlStatus::SoundInitializationFailed };
    }
    if (record->playbackState != SoundRecord::PlaybackState::Playing || record->sound->AtEnd()) {
        record->sound->Stop();
        if (record->sound->SeekSeconds(0.0F) != MA_SUCCESS || record->sound->Start() != MA_SUCCESS) {
            return { .status = kb::audio::AudioSourceControlStatus::PlaybackOperationFailed };
        }
        record->playbackState = SoundRecord::PlaybackState::Playing;
        record->resumeFrame = 0U;
    }
    record->previousPosition = FiniteOrZero(transform->worldPosition);
    record->hasPreviousPosition = false;
    return { .status = kb::audio::AudioSourceControlStatus::Success, .playing = true };
}

kb::audio::AudioSourceControlResult MiniaudioSourceRegistry::PauseSource(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    bool playbackAvailable) {
    const kb::audio::AudioSourceControlResult validation = ValidateSource(scene, entity, playbackAvailable);
    if (!validation.Succeeded()) {
        return validation;
    }
    const auto iterator = sounds_.find(entity.Id());
    if (iterator == sounds_.end() || iterator->second.sound == nullptr || iterator->second.playbackState != SoundRecord::PlaybackState::Playing) {
        return { .status = kb::audio::AudioSourceControlStatus::NotPlaying };
    }
    if (!iterator->second.sound->IsPlaying()) {
        if (iterator->second.sound->AtEnd()) {
            iterator->second.playbackState = SoundRecord::PlaybackState::Stopped;
            iterator->second.resumeFrame = 0U;
        }
        return { .status = kb::audio::AudioSourceControlStatus::NotPlaying };
    }
    iterator->second.sound->Stop();
    iterator->second.playbackState = SoundRecord::PlaybackState::Paused;
    return { .status = kb::audio::AudioSourceControlStatus::Success, .playing = false };
}

kb::audio::AudioSourceControlResult MiniaudioSourceRegistry::ResumeSource(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    bool playbackAvailable) {
    const kb::audio::AudioSourceControlResult validation = ValidateSource(scene, entity, playbackAvailable);
    if (!validation.Succeeded()) {
        return validation;
    }
    const auto iterator = sounds_.find(entity.Id());
    if (iterator == sounds_.end() || iterator->second.sound == nullptr || iterator->second.playbackState != SoundRecord::PlaybackState::Paused) {
        return { .status = kb::audio::AudioSourceControlStatus::NotPaused };
    }
    if (iterator->second.sound->AtEnd()) {
        iterator->second.playbackState = SoundRecord::PlaybackState::Stopped;
        iterator->second.resumeFrame = 0U;
        return { .status = kb::audio::AudioSourceControlStatus::NotPaused };
    }
    if (iterator->second.sound->Start() != MA_SUCCESS) {
        return { .status = kb::audio::AudioSourceControlStatus::PlaybackOperationFailed };
    }
    iterator->second.playbackState = SoundRecord::PlaybackState::Playing;
    return { .status = kb::audio::AudioSourceControlStatus::Success, .playing = true };
}

kb::audio::AudioSourceControlResult MiniaudioSourceRegistry::StopSource(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    bool playbackAvailable) {
    const kb::audio::AudioSourceControlResult validation = ValidateSource(scene, entity, playbackAvailable);
    if (!validation.Succeeded()) {
        return validation;
    }
    const auto iterator = sounds_.find(entity.Id());
    if (iterator != sounds_.end() && iterator->second.sound != nullptr) {
        iterator->second.sound->Stop();
        iterator->second.playbackState = SoundRecord::PlaybackState::Stopped;
    }
    return { .status = kb::audio::AudioSourceControlStatus::Success, .playing = false };
}

kb::audio::AudioSourceControlResult MiniaudioSourceRegistry::IsSourcePlaying(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    bool playbackAvailable) {
    const kb::audio::AudioSourceControlResult validation = ValidateSource(scene, entity, playbackAvailable);
    if (!validation.Succeeded()) {
        return validation;
    }
    const auto iterator = sounds_.find(entity.Id());
    if (iterator == sounds_.end() || iterator->second.sound == nullptr) {
        return { .status = kb::audio::AudioSourceControlStatus::Success, .playing = false };
    }
    const bool playing = iterator->second.playbackState == SoundRecord::PlaybackState::Playing && iterator->second.sound->IsPlaying();
    if (!playing && iterator->second.playbackState == SoundRecord::PlaybackState::Playing && iterator->second.sound->AtEnd()) {
        iterator->second.playbackState = SoundRecord::PlaybackState::Stopped;
    }
    return { .status = kb::audio::AudioSourceControlStatus::Success, .playing = playing };
}

void MiniaudioSourceRegistry::RemoveUnseenSounds() {
    for (auto iterator = sounds_.begin(); iterator != sounds_.end();) {
        if (seenEntities_.contains(iterator->first)) {
            ++iterator;
            continue;
        }
        iterator = sounds_.erase(iterator);
    }
}

void MiniaudioSourceRegistry::RemoveSound(std::uint64_t entityId) {
    sounds_.erase(entityId);
}

} // namespace kb::audio_miniaudio
