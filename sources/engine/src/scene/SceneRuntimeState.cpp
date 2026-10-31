#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneAnimators.hpp"
#include "ecs/FlecsEntityIds.hpp"
#include "ecs/world/WorldInternalAccess.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/transform/SceneTransformRootQueryCache.hpp"

#include <flecs.h>
#include <algorithm>
#include <exception>
#include <cstring>
#include <unordered_set>

namespace kb::scene {
namespace {

struct SceneValues {
#define KB_RUNTIME_FIELD(name) decltype(SceneState::name) name;
#include "scene/SceneRuntimeStateFields.inl"
#undef KB_RUNTIME_FIELD
};

bool CheckBoundary(const SceneState& state, std::string& error) {
    try { state.world.ValidateStructuralChangeAllowed("SceneRuntimeCheckpoint"); }
    catch (const std::exception& exception) { error = exception.what(); return false; }
    if (ecs_is_deferred(state.world.NativeHandle()) || ecs_stage_is_readonly(state.world.NativeHandle())) {
        error = "Scene checkpoint requires completed backend structural commands"; return false;
    }
    if (state.world.ShouldQuit()) { error = "A terminated world cannot resume from a checkpoint"; return false; }
    if (state.transformPassRunning || state.fixedTransformCapturing || state.historyRecording) {
        error = "Scene checkpoint requires an idle runtime boundary"; return false;
    }
    if (!state.systemScheduler.ExecutionOrderSnapshot().empty()) {
        error = "ECS systems require runtime state providers before scene checkpoints"; return false;
    }
    if (!state.contentInstances.empty() || !state.scriptPendingAssets.empty()
        || state.streaming || state.worldPartition || state.prefabs.Count() != 0U || state.prefabInstances.Count() != 0U
        || state.navigation.polygons || state.navigation.crowd || !state.tasks.empty()
        || state.audioPlaybackBackend || state.particleSimulationBackend || state.tilemapRuntime || state.physics2DRuntime
        || state.particleGpuEmitterConsumer || state.particleRenderConsumerId != 0U || state.pendingScreenCaptureId != 0U) {
        error = "Active scene subsystem requires a runtime state provider before checkpointing"; return false;
    }
    for (const auto& [id, timeline] : state.timelines) {
        static_cast<void>(id);
        if (!timeline.asset->audioTracks.empty() || std::any_of(timeline.segments.begin(), timeline.segments.end(), [](const auto& segment) {
            return !segment.source.asset->audioTracks.empty() || std::any_of(segment.audioVoices.begin(), segment.audioVoices.end(),
                [](const auto& voice) { return voice.id != 0U; });
        })) { error = "Audio timeline checkpoint requires an audio state provider"; return false; }
    }
    error.clear();
    return true;
}

using AssetVersion = std::pair<kb::assets::AssetId, std::uint64_t>;

std::vector<AssetVersion> AnimationAssetVersions(const SceneValues& values) {
    std::vector<AssetVersion> result;
    const auto add = [&](const auto& handle, std::uint64_t generation) {
        if (handle.Id().value != 0U) result.emplace_back(handle.Id(), generation);
    };
    const auto animator = [&](const AnimatorInstance& instance) {
        add(instance.controller, instance.controllerLoadGeneration);
        if (instance.skeleton) add(instance.skeleton->asset, instance.skeleton->loadGeneration);
        for (const auto& layer : instance.layers) for (const auto& state : layer.states) {
            add(state.blendSpace, state.blendSpaceLoadGeneration);
            for (const auto& motion : state.motions) add(motion.clip, motion.clipLoadGeneration);
        }
    };
    for (const auto& [id, instance] : values.animators) { static_cast<void>(id); animator(instance); }
    for (const auto& [id, control] : values.timelineAnimatorControls) {
        static_cast<void>(id); if (control.previous) animator(*control.previous);
    }
    for (const auto& [id, pose] : values.skeletonBindingPoses) { static_cast<void>(id); add(pose.skeleton.asset, pose.skeleton.loadGeneration); }
    for (const auto& [key, clip] : values.retargetedClips) {
        static_cast<void>(clip);
        for (std::size_t index=0; index<key.size(); index+=2U) result.emplace_back(kb::assets::AssetId{key[index]}, key[index+1U]);
    }
    for (const auto& [id, timeline] : values.timelines) {
        static_cast<void>(id); add(timeline.asset, timeline.assetLoadGeneration);
        for (const auto& segment : timeline.segments) {
            add(segment.source.asset, segment.assetLoadGeneration);
            for (const auto& animation : segment.animations) add(animation.asset, animation.assetLoadGeneration);
        }
    }
    return result;
}

bool ValidateAnimationAssets(const kb::assets::AssetManager& assets, std::span<const AssetVersion> versions, std::string& error) {
    for (const auto& [id, generation] : versions) if (assets.LoadGeneration(id) != generation) {
        error = "Animation checkpoint asset generation changed"; return false;
    }
    return true;
}

bool IsHierarchyOrName(ecs_id_t id) {
    return ECS_IS_PAIR(id) && (ECS_PAIR_FIRST(id) == EcsChildOf || id == ecs_pair(ecs_id(EcsIdentifier), EcsName));
}

std::vector<ecs_id_t> MetadataIds(const kb::ecs::World& world, kb::ecs::Entity entity) {
    std::vector<ecs_id_t> result;
    if (!world.BackendEntityAlive(entity)) return result;
    const auto* type = ecs_get_type(world.NativeHandle(), kb::ecs::FlecsEntityId(entity));
    if (!type) return result;
    for (int32_t index = 0; index < type->count; ++index) {
        const auto id = type->array[index];
        if (IsHierarchyOrName(id)) continue;
        const auto* info = ecs_get_type_info(world.NativeHandle(), id);
        if (!info || info->size == 0) result.push_back(id);
    }
    return result;
}

} // namespace

struct SceneRuntime::RuntimeSnapshot final : kb::core::RuntimeStateSnapshot {
    struct EntityMetadata {
        kb::ecs::Entity entity;
        kb::ecs::Entity parent;
        std::string name;
        std::vector<ecs_id_t> ids;
    };
    struct Provider {
        SceneSystemHandle handle;
        std::unique_ptr<kb::core::RuntimeStateSnapshot> snapshot;
    };
    struct SideValue {
        kb::ecs::Entity entity;
        kb::ecs::ComponentId component = 0U;
        std::vector<std::byte> bytes;
    };
    std::uint64_t sceneId = 0U;
    std::shared_ptr<const kb::ecs::ChunkedWorldSnapshot> world;
    std::optional<kb::ecs::ChunkedWorldDeltaSnapshot> delta;
    SceneValues values;
    std::vector<AssetVersion> animationAssets;
    std::uint64_t hierarchyVersion = 0U;
    std::vector<EntityMetadata> entities;
    std::vector<SceneTransformResidual> residuals;
    std::vector<SceneRuntimeCommand> commands;
    std::vector<Provider> providers;
    std::vector<SideValue> sideValues;
    std::size_t payload = 0U;
    [[nodiscard]] std::size_t PayloadBytes() const noexcept override { return payload; }

    bool Apply(Scene& scene, std::span<const SceneSystemScheduler::RuntimeStateBinding> bindings, std::string& error) const {
        auto& state = SceneAccess::State(scene);
        scene.Animators().WaitForDebugSnapshot();
        const auto replaceNative = [&](const kb::ecs::ChunkedWorldSnapshot& full) {
            auto current = state.world.CaptureChunkedSnapshot();
            std::unordered_set<kb::ecs::Entity::IdType> targetIds;
            targetIds.reserve(full.entityCount);
            for (const auto& chunk : full.chunks) for (const auto id : chunk.entityIds) targetIds.insert(id);
            kb::ecs::ChunkedWorldDeltaSnapshot delta;
            delta.entityCount = full.entityCount;
            delta.componentTypes = current.componentTypes;
            for (const auto& type : full.componentTypes) {
                if (std::none_of(delta.componentTypes.begin(), delta.componentTypes.end(), [&](const auto& t) { return t.id == type.id; }))
                    delta.componentTypes.push_back(type);
            }
            std::vector<kb::ecs::Entity> currentEntities;
            currentEntities.reserve(current.entityCount);
            for (const auto& chunk : current.chunks) for (const auto id : chunk.entityIds) {
                currentEntities.emplace_back(id);
                if (!targetIds.contains(id)) delta.destroyedEntityIds.push_back(id);
            }
            delta.chunks.reserve(full.chunks.size());
            for (const auto& chunk : full.chunks) delta.chunks.push_back({
                .archetypeIndex=chunk.archetypeIndex, .chunkIndex=chunk.chunkIndex, .fullArchetype=true,
                .entityIds=chunk.entityIds, .components=chunk.components});
            // Detach before destroying future parents: a restored child may survive
            // under another parent, and must not be cascade-deleted with the future one.
            for (const auto entity : currentEntities) state.world.SetName(entity, {});
            state.world.ClearParents(currentEntities);
            for (const auto entity : currentEntities) {
                for (const auto id : MetadataIds(state.world, entity))
                    ecs_remove_id(state.world.NativeHandle(), kb::ecs::FlecsEntityId(entity), id);
            }
            if (!state.world.ApplyChunkedDeltaSnapshot(delta)) { error = "Exact native ECS checkpoint restore failed"; return false; }
            return true;
        };
        if (!replaceNative(*world)) return false;
        if (delta && !state.world.ApplyChunkedDeltaSnapshot(*delta)) {
            error = "Native ECS scene delta restore failed"; return false;
        }
        for (const auto& value : sideValues) kb::ecs::WorldInternalAccess::SetComponent(
            state.world, value.entity, value.component, value.bytes.size(), value.bytes.data());
        for (const auto& metadata : entities) {
            if (metadata.parent.IsValid()) state.world.SetParent(metadata.entity, metadata.parent);
            for (const auto id : metadata.ids) ecs_add_id(state.world.NativeHandle(), kb::ecs::FlecsEntityId(metadata.entity), id);
        }
        for (const auto& metadata : entities) state.world.SetName(metadata.entity, metadata.name);
        const auto nextAnimator = state.nextAnimatorRuntimeBindingGeneration;
        const auto nextTimeline = state.nextTimelineInstanceId;
        const auto nextTimer = state.nextTimerId;
        const auto nextTask = state.nextTaskId;
        const auto nextMaterial = state.nextMaterialInstanceId;
        const auto nextLoaded = state.nextLoadedSceneId;
        const auto nextParameters = state.nextScriptPrefabParameterSetId;
        const auto nextScreenCapture = state.nextScreenCaptureId;
#define KB_RUNTIME_FIELD(name) state.name = values.name;
#include "scene/SceneRuntimeStateFields.inl"
#undef KB_RUNTIME_FIELD
        state.nextAnimatorRuntimeBindingGeneration = std::max(nextAnimator, state.nextAnimatorRuntimeBindingGeneration);
        state.nextTimelineInstanceId = std::max(nextTimeline, state.nextTimelineInstanceId);
        state.nextTimerId = std::max(nextTimer, state.nextTimerId);
        state.nextTaskId = std::max(nextTask, state.nextTaskId);
        state.nextMaterialInstanceId = std::max(nextMaterial, state.nextMaterialInstanceId);
        state.nextLoadedSceneId = std::max(nextLoaded, state.nextLoadedSceneId);
        state.nextScriptPrefabParameterSetId = std::max(nextParameters, state.nextScriptPrefabParameterSetId);
        state.nextScreenCaptureId = std::max(nextScreenCapture, state.nextScreenCaptureId);
        state.runtimeCommands.RestorePending(commands);
        state.transformResiduals.Clear();
        for (const auto& residual : residuals) state.transformResiduals.Acquire(SceneEntity{residual.entityId}) = residual;
        // Borrowed rows and derived caches must observe the new native storage.
        ++state.hierarchyTopologyVersion;
        const auto updateHierarchy = [&](auto& record) {
            if (record.observedHierarchyTopologyVersion == hierarchyVersion)
                record.observedHierarchyTopologyVersion = state.hierarchyTopologyVersion;
        };
        for (auto& [id, instance] : state.animators) { static_cast<void>(id); updateHierarchy(instance); }
        for (auto& [id, control] : state.timelineAnimatorControls) {
            static_cast<void>(id); if (control.previous) updateHierarchy(*control.previous);
        }
        for (auto& [id, timeline] : state.timelines) { static_cast<void>(id); updateHierarchy(timeline); }
        state.animatorParallelPoseScratch.clear();
        AnimatorDebugSnapshot debug;
        debug.revision = ++state.animatorDebugSnapshotRevision;
        state.animatorDebugSnapshots.Publish(std::move(debug));
        ++state.hierarchyRootAppendEpoch;
        state.transformTopology = {};
        state.transformRootQueryCache.reset();
        state.transformPassRecords.clear();
        state.transformPassRecordsVersion = 0U;
        state.transformValueDenseScratch.clear();
        state.transformValueSparseScratch.clear();
        state.denseTransformWorldScratchValid.clear();
        ++state.transformValueCacheBuildVersion;
        state.transformDirtyFrontierEntities.clear();
        state.transformDirtyFrontierDenseMarkedEntities.clear();
        state.transformDirtyFrontierDenseMarkEpochs.clear();
        state.transformDirtyFrontierSparseMarkEpochs.clear();
        state.renderProxyDenseComponentMasks.clear();
        state.renderProxySparseComponentMasks.clear();
        state.transformUpdatedBits.clear();
        state.transformUpdatedSparseEntities.clear();
        state.transformRenderProxyUpdateEntities.clear();
        state.transformRenderProxyWorldAffine3x4.clear();
        state.renderProxyUpdateEntities.clear();
        state.renderProxyUpdateEntityIds.clear();
        state.transformRenderProxyListsStale = true;
        ++state.renderProxyUpdateRevision;
        ++state.renderTopologyVersion;
        state.fixedTransformRecordsBuilt = false;
        ++state.interpolatedEntitiesVersion;
        for (std::size_t index=0; index<providers.size(); ++index) {
            if (providers[index].snapshot && !bindings[index].second->RestoreRuntimeState(*providers[index].snapshot, error)) return false;
        }
        SceneRuntimeReadSnapshot read;
        read.revision = ++state.runtimeSnapshotRevision;
        read.frameIndex = state.frameIndex;
        read.fixedStepIndex = state.fixedStepIndex;
        read.elapsedSeconds = state.elapsedSeconds;
        read.playing = state.isPlaying;
        read.shouldQuit = state.world.ShouldQuit();
        read.timeScale = state.timeScale;
        state.runtimeSnapshots.Publish(std::move(read));
        error.clear();
        return true;
    }
};

std::unique_ptr<kb::core::RuntimeStateSnapshot> SceneRuntime::CaptureRuntimeState(std::string& error) {
    auto& state = SceneAccess::State(scene_);
    if (!CheckBoundary(state, error)) return {};
    scene_.Animators().WaitForDebugSnapshot();
    std::vector<SceneSystemScheduler::RuntimeStateBinding> bindings;
    if (!state.sceneSystemScheduler.CollectRuntimeStateProviders(bindings, error)) return {};
    const auto operation = state.sceneSystemScheduler.BeginRuntimeStateOperation();
    auto snapshot = std::make_unique<RuntimeSnapshot>();
    snapshot->sceneId = scene_.Id();
    // Providers finish pending simulation before ECS and scene values are copied.
    for (const auto& [handle, provider] : bindings) {
        if (!provider) { snapshot->providers.push_back({handle, {}}); continue; }
        auto captured = provider->CaptureRuntimeState(error);
        if (!captured) return {};
        snapshot->payload += captured->PayloadBytes();
        snapshot->providers.push_back({handle, std::move(captured)});
    }
    snapshot->world = std::make_shared<const kb::ecs::ChunkedWorldSnapshot>(state.world.CaptureChunkedSnapshot());
    snapshot->entities.reserve(snapshot->world->entityCount);
    for (const auto& chunk : snapshot->world->chunks) {
        snapshot->payload += chunk.entityIds.size() * sizeof(kb::ecs::Entity::IdType);
        for (const auto& column : chunk.components) snapshot->payload += column.data.size();
        for (const auto id : chunk.entityIds) {
            const kb::ecs::Entity entity{id};
            auto& metadata = snapshot->entities.emplace_back();
            metadata.entity = entity;
            metadata.parent = state.world.Parent(entity);
            metadata.name = state.world.Name(entity);
            metadata.ids = MetadataIds(state.world, entity);
            std::unordered_set<kb::ecs::ComponentId> captured;
            for (const auto& column : chunk.components) captured.insert(column.componentId);
            for (const auto& type : snapshot->world->componentTypes) {
                if (captured.contains(type.id) || !kb::ecs::UsesNativeSideStorage(type.storageClass)) continue;
                const auto* data = static_cast<const std::byte*>(kb::ecs::WorldInternalAccess::TryGetComponent(state.world, entity, type.id));
                if (!data) continue;
                auto& side = snapshot->sideValues.emplace_back();
                side.entity = entity; side.component = type.id;
                side.bytes.assign(data, data + type.size);
                snapshot->payload += type.size;
                captured.insert(type.id);
            }
            if (state.world.BackendEntityAlive(entity)) {
                const auto* backendType = ecs_get_type(state.world.NativeHandle(), kb::ecs::FlecsEntityId(entity));
                if (backendType) for (int32_t index=0; index<backendType->count; ++index) {
                    const auto idValue = backendType->array[index];
                    if (IsHierarchyOrName(idValue)) continue;
                    const auto* info = ecs_get_type_info(state.world.NativeHandle(), idValue);
                    if (info && info->size != 0 && !captured.contains(idValue)) {
                        error = "Backend-only component data requires a runtime state provider"; return {};
                    }
                }
            }
            snapshot->payload += metadata.name.size() + metadata.ids.size() * sizeof(ecs_id_t);
            if (const auto* residual = state.transformResiduals.Find(SceneEntity{id})) snapshot->residuals.push_back(*residual);
        }
    }
#define KB_RUNTIME_FIELD(name) snapshot->values.name = state.name;
#include "scene/SceneRuntimeStateFields.inl"
#undef KB_RUNTIME_FIELD
    snapshot->hierarchyVersion = state.hierarchyTopologyVersion;
    snapshot->animationAssets = AnimationAssetVersions(snapshot->values);
    if (!ValidateAnimationAssets(state.assets, snapshot->animationAssets, error)) return {};
    snapshot->commands = state.runtimeCommands.CapturePending();
    error.clear();
    return snapshot;
}

std::unique_ptr<kb::core::RuntimeStateSnapshot> SceneRuntime::CaptureRuntimeDeltaState(
    const kb::core::RuntimeStateSnapshot& baseline, std::string& error) {
    if (!ValidateRuntimeState(baseline, error)) return {};
    const auto& base = static_cast<const RuntimeSnapshot&>(baseline);
    if (base.delta) { error = "Scene delta capture requires a full checkpoint baseline"; return {}; }
    auto captured = CaptureRuntimeState(error);
    if (!captured) return {};
    auto& snapshot = static_cast<RuntimeSnapshot&>(*captured);
    // The provider boundary has already completed; native capture is read-only.
    auto delta = SceneAccess::State(scene_).world.CaptureChunkedDeltaSnapshot(*base.world);
    for (const auto& chunk : snapshot.world->chunks) {
        snapshot.payload -= chunk.entityIds.size() * sizeof(kb::ecs::Entity::IdType);
        for (const auto& column : chunk.components) snapshot.payload -= column.data.size();
    }
    snapshot.payload += delta.destroyedEntityIds.size() * sizeof(kb::ecs::Entity::IdType);
    for (const auto& chunk : delta.chunks) {
        snapshot.payload += chunk.entityIds.size() * sizeof(kb::ecs::Entity::IdType);
        for (const auto& column : chunk.components) snapshot.payload += column.data.size();
    }
    snapshot.delta = std::move(delta);
    // Retain just the immutable native baseline, independently of its providers.
    snapshot.world = base.world;
    error.clear();
    return captured;
}

bool SceneRuntime::ValidateRuntimeState(const kb::core::RuntimeStateSnapshot& source, std::string& error) const {
    const auto* snapshot = dynamic_cast<const RuntimeSnapshot*>(&source);
    if (!snapshot || snapshot->sceneId != scene_.Id()) { error = "Scene checkpoint belongs to another scene lifetime"; return false; }
    const auto& state = SceneAccess::State(scene_);
    if (!CheckBoundary(state, error)) return false;
    if (!ValidateAnimationAssets(state.assets, snapshot->animationAssets, error)) return false;
    std::vector<SceneSystemScheduler::RuntimeStateBinding> bindings;
    if (!state.sceneSystemScheduler.CollectRuntimeStateProviders(bindings, error)) return false;
    if (bindings.size() != snapshot->providers.size()) { error = "Scene system topology changed since checkpoint"; return false; }
    for (std::size_t index=0; index<bindings.size(); ++index) if (bindings[index].first != snapshot->providers[index].handle) {
        error = "Scene provider lifetime changed since checkpoint"; return false;
    }
    for (std::size_t index=0; index<bindings.size(); ++index) if ((bindings[index].second == nullptr) != (snapshot->providers[index].snapshot == nullptr)) {
        error = "Scene system checkpoint ownership changed since capture"; return false;
    }
    error.clear();
    return true;
}

bool SceneRuntime::RestoreRuntimeState(const kb::core::RuntimeStateSnapshot& source, std::string& error) {
    if (!ValidateRuntimeState(source, error)) return false;
    auto backup = CaptureRuntimeState(error);
    if (!backup) return false;
    std::vector<SceneSystemScheduler::RuntimeStateBinding> bindings;
    auto& state = SceneAccess::State(scene_);
    if (!state.sceneSystemScheduler.CollectRuntimeStateProviders(bindings, error)) return false;
    const auto& snapshot = static_cast<const RuntimeSnapshot&>(source);
    const auto operation = state.sceneSystemScheduler.BeginRuntimeStateOperation();
    try { if (snapshot.Apply(scene_, bindings, error)) return true; }
    catch (const std::exception& exception) { error = exception.what(); }
    const auto failure = error;
    std::string rollback;
    bool restored = false;
    try { restored = static_cast<const RuntimeSnapshot&>(*backup).Apply(scene_, bindings, rollback); }
    catch (const std::exception& exception) { rollback = exception.what(); }
    error = failure + (restored ? "; previous state restored" : "; rollback failed: " + rollback);
    return false;
}

} // namespace kb::scene
