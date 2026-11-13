#include "ecs/world/WorldInternalAccess.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneHistoryService.hpp"
#include "scene/SceneState.hpp"
#include "scene/SceneTransformService.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"
#include "scene/transform/SceneTransformHierarchySystem.hpp"
#include "scene/transform/SceneTransformDirtyFrontier.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace kb::scene {
namespace {

inline constexpr std::size_t kParallelSetManyGrainSize = 2048U;

// The batch lists every entity once (by dense index), so its rows can be written on several threads with the result
// of the serial loop; a repeated entry, where the last value wins, keeps the batch serial.
[[nodiscard]] bool ListsEntitiesOnce(SceneState& state, std::span<const SceneEntity> entities) {
    std::vector<std::uint64_t>& marks = state.transformSetManyMarksScratch;
    const std::size_t words = (state.denseHierarchyParents.size() + 63U) / 64U;
    if (marks.size() < words) {
        marks.resize(words, 0U);
    }
    std::atomic_bool once{ true };
    state.transformWorkerPool->ParallelForChunks(entities.size(), kParallelSetManyGrainSize, [&marks, entities, &once](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
        std::size_t word = std::numeric_limits<std::size_t>::max();
        std::uint64_t bits = 0U;
        bool chunkOnce = true;
        const auto publish = [&marks, &word, &bits, &chunkOnce] {
            if (bits != 0U && (std::atomic_ref<std::uint64_t>{ marks[word] }.fetch_or(bits, std::memory_order_relaxed) & bits) != 0U) chunkOnce = false;
        };
        for (std::size_t index = chunk.begin; index < chunk.begin + chunk.count && chunkOnce; ++index) {
            const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entities[index]);
            if (denseIndex == kb::ecs::kInvalidGeneratedEntityIndex || denseIndex / 64U >= marks.size()) {
                chunkOnce = false;
                break;
            }
            if (denseIndex / 64U != word) {
                publish();
                word = denseIndex / 64U;
                bits = 0U;
            }
            const std::uint64_t bit = std::uint64_t{ 1U } << (denseIndex % 64U);
            chunkOnce = (bits & bit) == 0U;
            bits |= bit;
        }
        publish();
        if (!chunkOnce) once.store(false, std::memory_order_relaxed);
    });
    std::ranges::fill(marks, 0U);
    return once.load(std::memory_order_relaxed);
}

// The stored row a write changes in place, found with one lookup: a row that composes on write is noted as written
// (its world transform is composed when it is published), any other row is flagged for the sync. `row` is null when
// the entity has no row in the native storage.
SceneTransformWrite OpenTransformRow(SceneState& state, SceneEntity entity) {
    const kb::ecs::ComponentId transformId = state.components.TransformComponentId();
    SceneTransformWrite write{ .entity = entity };
    if (state.world.MirrorsValueWrites(transformId)) {
        write.mirrored = true;
        write.row = static_cast<TransformComponent*>(kb::ecs::WorldInternalAccess::TryGetMutableNativeComponent(state.world, entity, transformId));
    } else if (SceneTransformComposesOnWrite(state, entity)) {
        write.composes = true;
        auto& storage = const_cast<kb::ecs::NativeArchetypeStorage&>(state.world.NativeStorage());
        write.row = static_cast<TransformComponent*>(storage.TryGetMutableComponentDataNoteWritten(entity, transformId));
    } else {
        write.row = static_cast<TransformComponent*>(kb::ecs::WorldInternalAccess::TryGetMutableNativeComponentMarkModified(state.world, entity, transformId));
    }
    return write;
}

// Publishes a row opened by OpenTransformRow after its local fields and versions were written: a row that composes
// on write gets its world transform here, as the sync would compose it; an observed row is flagged and mirrored;
// a row with parent or children is composed by the sync from the frontier.
void PublishTransformRow(SceneState& state, const SceneTransformWrite& write) {
    const SceneEntity entity = write.entity;
    TransformComponent& row = *write.row;
    if (write.mirrored) {
        state.componentStorage.Transforms().MarkWritten(entity);
    } else if (write.composes) {
        ComposeSceneTransformRoot(row);
        PrepareSceneTransformUpdateRecording(state);
        RecordComposedSceneTransform(state, entity, row);
    }
    if (SceneHierarchyCache::HasTransformLink(state, entity)) {
        EnqueueSceneTransformDirtyFrontier(state, entity);
    }
    MarkScenePrefabNodeDirty(state, entity);
}

// Writes a live entity's transform. False when the entity is not alive.
bool WriteTransform(SceneState& state, SceneEntity entity, const TransformComponent& transform) {
    if (const SceneTransformWrite write = OpenTransformRow(state, entity); write.row != nullptr) {
        SceneTransformComponentStore::Write(*write.row, transform);
        PublishTransformRow(state, write);
        return true;
    }
    // A live entity without a stored row gets one the structural way.
    if (!state.componentStorage.Transforms().Set(entity, transform)) {
        return false;
    }
    if (SceneHierarchyCache::HasTransformLink(state, entity)) {
        EnqueueSceneTransformDirtyFrontier(state, entity);
    }
    MarkScenePrefabNodeDirty(state, entity);
    return true;
}

} // namespace

TransformComponent SceneTransformService::Get(const Scene& scene, SceneObject object) {
    return SceneEntityService::IsAlive(scene, object) ? Get(scene, object.Entity()) : TransformComponent{};
}

TransformComponent SceneTransformService::Get(const Scene& scene, SceneEntity entity) {
    const TransformComponent* transform = TryGet(scene, entity);
    return transform == nullptr ? TransformComponent{} : *transform;
}

const TransformComponent* SceneTransformService::TryGet(const Scene& scene, SceneEntity entity) noexcept {
    return SceneAccess::State(scene).componentStorage.Transforms().TryGet(entity);
}

TransformComponent* SceneTransformService::TryGet(Scene& scene, SceneEntity entity) noexcept {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    return SceneAccess::State(scene).componentStorage.Transforms().TryGet(entity);
}

void SceneTransformService::Set(Scene& scene, SceneObject object, const TransformComponent& transform) {
    if (SceneAccess::BelongsTo(scene, object)) {
        Set(scene, object.Entity(), transform);
    }
}

void SceneTransformService::Set(Scene& scene, SceneEntity entity, const TransformComponent& transform) {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    static_cast<void>(WriteTransform(SceneAccess::State(scene), entity, transform));
}

SceneTransformWrite SceneTransformService::BeginWrite(Scene& scene, SceneEntity entity) noexcept {
    SceneState& state = SceneAccess::State(scene);
    SceneTransformWrite write = OpenTransformRow(state, entity);
    if (write.row != nullptr) {
        SceneHistoryService::NoteObjectChanging(scene, entity);
    }
    write.structure = state.world.NativeStorage().StructuralVersion();
    return write;
}

void SceneTransformService::EndWrite(Scene& scene, const SceneTransformWrite& write) {
    if (write.row == nullptr) {
        return;
    }
    SceneState& state = SceneAccess::State(scene);
    const SceneTransformWrite current = write.structure == state.world.NativeStorage().StructuralVersion() ? write : OpenTransformRow(state, write.entity);
    if (current.row == nullptr) {
        return;
    }
    ++current.row->localVersion;
    current.row->worldDirty = true;
    PublishTransformRow(state, current);
}

void SceneTransformService::SetMany(Scene& scene, std::span<const SceneEntity> entities, std::span<const TransformComponent> transforms) {
    if (entities.size() != transforms.size()) {
        throw std::invalid_argument("Scene transform batch write requires one transform per entity");
    }
    for (const SceneEntity entity : entities) {
        SceneHistoryService::NoteObjectChanging(scene, entity);
    }
    SceneState& state = SceneAccess::State(scene);
    const bool trackPrefab = !state.suppressPrefabDirtyTracking && state.prefabInstances.Count() > 0U;
    const auto set = [&state, entities, transforms](std::size_t index) {
        static_cast<void>(WriteTransform(state, entities[index], transforms[index]));
    };
    const std::uint64_t transformId = state.components.TransformComponentId();
    if (entities.size() >= kParallelSetManyGrainSize * 4U && !state.world.MirrorsValueWrites(transformId)) {
        EnsureSceneTransformWorkerPool(state);
    }
    if (entities.size() < kParallelSetManyGrainSize * 4U || state.world.MirrorsValueWrites(transformId) || !ListsEntitiesOnce(state, entities)) {
        // An observer sees each write as it happens, in order.
        for (std::size_t index = 0U; index < entities.size(); ++index) {
            set(index);
        }
        return;
    }

    // Each row is looked up and written by one worker; the flags, the frontier of linked rows and the prefab nodes
    // follow after the join, in batch order.
    std::vector<SceneState::TransformSetManyRange>& ranges = state.transformSetManyRangesScratch;
    const std::size_t rangeCount = (entities.size() + kParallelSetManyGrainSize - 1U) / kParallelSetManyGrainSize;
    if (ranges.size() < rangeCount) {
        ranges.resize(rangeCount);
    }
    auto& storage = const_cast<kb::ecs::NativeArchetypeStorage&>(state.world.NativeStorage());
    PrepareSceneTransformUpdateRecording(state);
    std::mutex sparseMutex;
    state.transformWorkerPool->ParallelForChunks(entities.size(), kParallelSetManyGrainSize,
        [&state, &storage, &ranges, &sparseMutex, transformId, trackPrefab, entities, transforms](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
            SceneState::TransformSetManyRange& range = ranges[chunk.index];
            range.writtenRows.clear();
            range.composedArchetypes.clear();
            range.linked.clear();
            range.prefabNodes.clear();
            range.unstored.clear();
            UpdatedTransformBitWriter updatedBits{ state, sparseMutex };
            for (std::size_t index = chunk.begin; index < chunk.begin + chunk.count; ++index) {
                const SceneEntity entity = entities[index];
                kb::ecs::NativeComponentRows row;
                void* data = storage.TryGetMutableComponentRow(entity, transformId, row);
                if (data == nullptr) {
                    if (state.world.IsAlive(entity)) range.unstored.push_back(index);
                    continue;
                }
                auto& current = *static_cast<TransformComponent*>(data);
                std::uint32_t nodeIndex = 0U;
                if (trackPrefab && state.prefabInstances.FindContainingEntity(entity, nodeIndex).IsValid()) range.prefabNodes.push_back(entity);
                if (SceneTransformComposesOnWrite(state, entity)) {
                    WriteAndComposeSceneTransformRoot(current, transforms[index]);
                    updatedBits.Record(entity, current);
                    if (range.composedArchetypes.empty() || range.composedArchetypes.back() != row.archetypeIndex) range.composedArchetypes.push_back(row.archetypeIndex);
                    continue;
                }
                SceneTransformComponentStore::Write(current, transforms[index]);
                kb::ecs::NativeComponentRows* run = range.writtenRows.empty() ? nullptr : &range.writtenRows.back();
                if (run != nullptr && run->archetypeIndex == row.archetypeIndex && run->chunkIndex == row.chunkIndex && run->firstRow + run->count == row.firstRow) {
                    ++run->count;
                } else {
                    range.writtenRows.push_back(row);
                }
                if (SceneHierarchyCache::HasTransformLink(state, entity)) range.linked.push_back(entity);
            }
        });
    state.transformSetManyRowsScratch.clear();
    for (std::size_t rangeIndex = 0U; rangeIndex < rangeCount; ++rangeIndex) {
        state.transformSetManyRowsScratch.insert(state.transformSetManyRowsScratch.end(), ranges[rangeIndex].writtenRows.begin(), ranges[rangeIndex].writtenRows.end());
    }
    storage.MarkComponentRowsModified(transformId, state.transformSetManyRowsScratch);
    std::size_t notedArchetype = std::numeric_limits<std::size_t>::max();
    for (std::size_t rangeIndex = 0U; rangeIndex < rangeCount; ++rangeIndex) {
        for (const std::size_t archetype : ranges[rangeIndex].composedArchetypes) {
            if (archetype != notedArchetype) storage.NoteComponentWritten(archetype, transformId);
            notedArchetype = archetype;
        }
    }
    for (std::size_t rangeIndex = 0U; rangeIndex < rangeCount; ++rangeIndex) {
        for (const SceneEntity entity : ranges[rangeIndex].linked) EnqueueSceneTransformDirtyFrontier(state, entity);
        for (const SceneEntity entity : ranges[rangeIndex].prefabNodes) MarkScenePrefabNodeDirty(state, entity);
    }
    // A live entity without a transform row gets one the structural way, after the rows above were flagged.
    for (std::size_t rangeIndex = 0U; rangeIndex < rangeCount; ++rangeIndex) {
        for (const std::size_t index : ranges[rangeIndex].unstored) set(index);
    }
}

SceneTransformWorkerWrites::SceneTransformWorkerWrites(Scene& scene, std::size_t rangeCount)
    : scene_(scene) {
    SceneState& state = SceneAccess::State(scene);
    // An undo recording notes each object before it changes, on its own thread; an observer sees each write in order.
    if (rangeCount == 0U || state.transformWorkerWritesOpen || state.historyRecording != nullptr ||
        state.world.MirrorsValueWrites(state.components.TransformComponentId())) {
        return;
    }
    PrepareSceneTransformUpdateRecording(state);
    if (state.transformWorkerRanges.size() < rangeCount) {
        state.transformWorkerRanges.resize(rangeCount);
    }
    for (std::size_t range = 0U; range < rangeCount; ++range) {
        SceneState::TransformWorkerRange& output = state.transformWorkerRanges[range];
        output.archetypes.clear();
        output.deferred.clear();
        output.identityAffine = 0U;
    }
    trackPrefab_ = !state.suppressPrefabDirtyTracking && state.prefabInstances.Count() > 0U;
    shared_ = rangeCount > 1U;
    state.transformWorkerWritesOpen = true;
    enabled_ = true;
}

SceneTransformWorkerWrites::~SceneTransformWorkerWrites() {
    try {
        Finish();
    } catch (...) {
    }
}

TransformComponent* SceneTransformWorkerWrites::Open(std::size_t range, SceneEntity entity) noexcept {
    SceneState& state = SceneAccess::State(scene_);
    auto& storage = const_cast<kb::ecs::NativeArchetypeStorage&>(state.world.NativeStorage());
    kb::ecs::NativeComponentRows row;
    void* data = storage.TryGetMutableComponentRow(entity, state.components.TransformComponentId(), row);
    if (data == nullptr) {
        return nullptr;
    }
    std::vector<std::size_t>& archetypes = state.transformWorkerRanges[range].archetypes;
    if (archetypes.empty() || archetypes.back() != row.archetypeIndex) {
        try {
            archetypes.push_back(row.archetypeIndex);
        } catch (...) {
            return nullptr;
        }
    }
    return static_cast<TransformComponent*>(data);
}

void SceneTransformWorkerWrites::Publish(std::size_t range, SceneEntity entity, TransformComponent& row) noexcept {
    SceneState& state = SceneAccess::State(scene_);
    SceneState::TransformWorkerRange& output = state.transformWorkerRanges[range];
    ++row.localVersion;
    row.worldDirty = true;
    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
    if (trackPrefab_ || denseIndex == kb::ecs::kInvalidGeneratedEntityIndex || denseIndex / 64U >= state.transformUpdatedBits.size() ||
        !SceneTransformComposesOnWrite(state, entity)) {
        try {
            output.deferred.push_back(entity);
        } catch (...) {
        }
        return;
    }
    ComposeSceneTransformRoot(row);
    const std::uint64_t bit = std::uint64_t{ 1U } << (denseIndex % 64U);
    std::uint64_t& word = state.transformUpdatedBits[denseIndex / 64U];
    std::uint64_t before = word;
    if (shared_) {
        before = std::atomic_ref<std::uint64_t>{ word }.fetch_or(bit, std::memory_order_relaxed);
    } else {
        word = before | bit;
    }
    if ((before & bit) == 0U && SceneTransformRootHotKernel::CanWriteIdentityAffineFastPath(row)) {
        ++output.identityAffine;
    }
}

void SceneTransformWorkerWrites::Finish() {
    if (!enabled_) {
        return;
    }
    enabled_ = false;
    SceneState& state = SceneAccess::State(scene_);
    state.transformWorkerWritesOpen = false;
    auto& storage = const_cast<kb::ecs::NativeArchetypeStorage&>(state.world.NativeStorage());
    const kb::ecs::ComponentId transformId = state.components.TransformComponentId();
    std::size_t noted = std::numeric_limits<std::size_t>::max();
    for (SceneState::TransformWorkerRange& output : state.transformWorkerRanges) {
        for (const std::size_t archetype : output.archetypes) {
            if (archetype != noted) storage.NoteComponentWritten(archetype, transformId);
            noted = archetype;
        }
        state.lastTransformRenderProxyIdentityAffineFastPathCount += output.identityAffine;
        output.identityAffine = 0U;
        output.archetypes.clear();
    }
    // The rows the workers could not publish go the way of EndWrite, in range order.
    for (SceneState::TransformWorkerRange& output : state.transformWorkerRanges) {
        for (const SceneEntity entity : output.deferred) {
            if (const SceneTransformWrite write = OpenTransformRow(state, entity); write.row != nullptr) {
                PublishTransformRow(state, write);
            }
        }
        output.deferred.clear();
    }
}

void SceneTransformService::MarkModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Transforms().MarkModified(entity);
        EnqueueSceneTransformDirtyFrontier(state, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

void SceneTransformService::MarkModified(Scene& scene, std::span<const SceneEntity> entities) noexcept {
    SceneState& state = SceneAccess::State(scene);
    // Resolve the prefab-tracking decision once for the whole batch: a scene with
    // no prefab instances skips all per-entity prefab containment lookups.
    const bool trackPrefab = !state.suppressPrefabDirtyTracking && state.prefabInstances.Count() > 0U;
    for (const SceneEntity entity : entities) {
        if (!SceneEntityService::IsAlive(scene, entity)) {
            continue;
        }
        state.componentStorage.Transforms().MarkModified(entity);
        EnqueueSceneTransformDirtyFrontier(state, entity);
        if (trackPrefab) {
            MarkScenePrefabNodeDirty(state, entity);
        }
    }
}

TransformPassStats SceneTransformService::ParallelForEachRoot(Scene& scene, std::size_t grainRows, std::span<const kb::ecs::ComponentId> extraComponents,
    SceneTransforms::TransformRangeBody body, void* context, bool declaredAccess) {
    return RunSceneTransformPass(SceneAccess::State(scene), grainRows, extraComponents, body, context, declaredAccess);
}

void SceneTransformService::SetInterpolated(Scene& scene, SceneEntity entity, bool interpolated) {
    SceneState& state = SceneAccess::State(scene);
    if (!state.world.IsAlive(entity)) {
        return;
    }
    const bool changed = interpolated ? state.interpolatedEntities.insert(entity.Id()).second : state.interpolatedEntities.erase(entity.Id()) != 0U;
    if (changed) {
        ++state.interpolatedEntitiesVersion;
    }
}

bool SceneTransformService::IsInterpolated(const Scene& scene, SceneEntity entity) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    if (!state.world.IsAlive(entity)) {
        return false;
    }
    if (state.interpolatedEntities.contains(entity.Id())) {
        return true;
    }
    const auto& components = state.components;
    for (const std::uint64_t componentId : { components.RigidbodyComponentId(), components.CharacterControllerComponentId(), components.JointComponentId() }) {
        if (componentId != 0U && state.world.NativeStorage().HasComponent(entity, componentId)) {
            return true;
        }
    }
    return false;
}

void SceneTransformService::MarkParentModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Transforms().MarkParentModified(entity);
        EnqueueSceneTransformDirtyFrontier(state, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

} // namespace kb::scene
