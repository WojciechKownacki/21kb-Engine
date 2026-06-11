#include "scene/MiniaudioSourceRegistry.hpp"

#include "assets/MiniaudioClipResolver.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "runtime/MiniaudioSound.hpp"

namespace kb::audio_miniaudio {
namespace {

[[nodiscard]] MiniaudioSoundSettings ToSoundSettings(
    const kb::scene::AudioSourceComponent& source,
    const kb::scene::TransformComponent& transform) noexcept {
    return MiniaudioSoundSettings{
        .volume = source.volume,
        .pitch = source.pitch,
        .loop = source.loop,
        .spatial = source.spatial,
        .position = transform.worldPosition,
    };
}

} // namespace

void MiniaudioSourceRegistry::Sync(
    ma_engine& engine,
    kb::scene::SceneSystemContext& context,
    const MiniaudioClipResolver& clipResolver,
    bool playbackAvailable) {
    seenEntities_.clear();
    SourceSyncContext syncContext{
        .registry = this,
        .engine = &engine,
        .scene = &context.GetScene(),
        .clipResolver = &clipResolver,
        .playbackAvailable = playbackAvailable,
    };
    context.Transforms().ForEach(&SyncSourceFromTransform, &syncContext);
    RemoveUnseenSounds();
}

void MiniaudioSourceRegistry::StopAll() noexcept {
    sounds_.clear();
}

void MiniaudioSourceRegistry::SyncSourceFromTransform(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, void* context) {
    auto* syncContext = static_cast<SourceSyncContext*>(context);
    syncContext->registry->SyncSource(
        *syncContext->engine,
        *syncContext->scene,
        entity,
        transform,
        *syncContext->clipResolver,
        syncContext->playbackAvailable);
}

void MiniaudioSourceRegistry::SyncSource(
    ma_engine& engine,
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    const kb::scene::TransformComponent& transform,
    const MiniaudioClipResolver& clipResolver,
    bool playbackAvailable) {
    const kb::scene::AudioSourceComponent* source = scene.Components().AudioSources().TryGet(entity);
    if (source == nullptr) {
        return;
    }

    seenEntities_.insert(entity.Id());
    if (!playbackAvailable) {
        RemoveSound(entity.Id());
        return;
    }

    const std::filesystem::path path = clipResolver.Resolve(scene, source->clipAssetId);
    if (path.empty()) {
        RemoveSound(entity.Id());
        return;
    }

    const SoundSignature signature{ .clipAssetId = source->clipAssetId, .path = path };
    SoundRecord* record = EnsureSound(engine, entity.Id(), signature, *source, transform);
    if (record == nullptr || record->sound == nullptr || !record->sound->IsInitialized()) {
        return;
    }

    record->sound->Apply(ToSoundSettings(*source, transform));
}

MiniaudioSourceRegistry::SoundRecord* MiniaudioSourceRegistry::EnsureSound(
    ma_engine& engine,
    std::uint64_t entityId,
    const SoundSignature& signature,
    const kb::scene::AudioSourceComponent& source,
    const kb::scene::TransformComponent& transform) {
    auto iterator = sounds_.find(entityId);
    if (iterator != sounds_.end() && iterator->second.signature == signature) {
        return &iterator->second;
    }

    if (iterator != sounds_.end()) {
        sounds_.erase(iterator);
    }

    auto sound = std::make_unique<MiniaudioSound>();
    if (sound->InitializeFromFile(engine, signature.path, source.spatial) != MA_SUCCESS) {
        return nullptr;
    }

    sound->Apply(ToSoundSettings(source, transform));
    if (source.autoplay) {
        static_cast<void>(sound->Start());
    }

    auto [inserted, _] = sounds_.try_emplace(entityId, SoundRecord{
        .signature = signature,
        .sound = std::move(sound),
    });
    return &inserted->second;
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
    const auto iterator = sounds_.find(entityId);
    if (iterator == sounds_.end()) {
        return;
    }
    sounds_.erase(iterator);
}

} // namespace kb::audio_miniaudio
