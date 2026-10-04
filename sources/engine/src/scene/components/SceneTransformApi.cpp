#include "scene/SceneAccess.hpp"
#include "scene/SceneEntityService.hpp"
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
    return SceneAccess::State(scene).componentStorage.Transforms().TryGet(entity);
}

void SceneTransformService::Set(Scene& scene, SceneObject object, const TransformComponent& transform) {
    if (SceneAccess::BelongsTo(scene, object)) {
        Set(scene, object.Entity(), transform);
    }
}

void SceneTransformService::Set(Scene& scene, SceneEntity entity, const TransformComponent& transform) {
    SceneState& state = SceneAccess::State(scene);
    if (state.componentStorage.Transforms().Set(entity, transform)) {
        // the sync composes a row without parent or children from its dirty flag alone
        if (SceneHierarchyCache::HasTransformLink(state, entity)) {
            EnqueueSceneTransformDirtyFrontier(state, entity);
        }
        MarkScenePrefabNodeDirty(state, entity);
    }
}

void SceneTransformService::SetMany(Scene& scene, std::span<const SceneEntity> entities, std::span<const TransformComponent> transforms) {
    if (entities.size() != transforms.size()) {
        throw std::invalid_argument("Scene transform batch write requires one transform per entity");
    }
    SceneState& state = SceneAccess::State(scene);
    const bool trackPrefab = !state.suppressPrefabDirtyTracking && state.prefabInstances.Count() > 0U;
    auto& store = state.componentStorage.Transforms();
    const auto set = [&state, &store, trackPrefab, entities, transforms](std::size_t index) {
        const SceneEntity entity = entities[index];
        if (!store.Set(entity, transforms[index])) {
            return;
        }
        if (SceneHierarchyCache::HasTransformLink(state, entity)) {
            EnqueueSceneTransformDirtyFrontier(state, entity);
        }
        if (trackPrefab) {
            MarkScenePrefabNodeDirty(state, entity);
        }
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
    state.transformWorkerPool->ParallelForChunks(entities.size(), kParallelSetManyGrainSize,
        [&state, &storage, &ranges, transformId, trackPrefab, entities, transforms](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
            SceneState::TransformSetManyRange& range = ranges[chunk.index];
            range.writtenRows.clear();
            range.linked.clear();
            range.prefabNodes.clear();
            range.unstored.clear();
            for (std::size_t index = chunk.begin; index < chunk.begin + chunk.count; ++index) {
                const SceneEntity entity = entities[index];
                kb::ecs::NativeComponentRows row;
                void* data = storage.TryGetMutableComponentRow(entity, transformId, row);
                if (data == nullptr) {
                    if (state.world.IsAlive(entity)) range.unstored.push_back(index);
                    continue;
                }
                SceneTransformComponentStore::Write(*static_cast<TransformComponent*>(data), transforms[index]);
                kb::ecs::NativeComponentRows* run = range.writtenRows.empty() ? nullptr : &range.writtenRows.back();
                if (run != nullptr && run->archetypeIndex == row.archetypeIndex && run->chunkIndex == row.chunkIndex && run->firstRow + run->count == row.firstRow) {
                    ++run->count;
                } else {
                    range.writtenRows.push_back(row);
                }
                if (SceneHierarchyCache::HasTransformLink(state, entity)) range.linked.push_back(entity);
                std::uint32_t nodeIndex = 0U;
                if (trackPrefab && state.prefabInstances.FindContainingEntity(entity, nodeIndex).IsValid()) range.prefabNodes.push_back(entity);
            }
        });
    state.transformSetManyRowsScratch.clear();
    for (std::size_t rangeIndex = 0U; rangeIndex < rangeCount; ++rangeIndex) {
        state.transformSetManyRowsScratch.insert(state.transformSetManyRowsScratch.end(), ranges[rangeIndex].writtenRows.begin(), ranges[rangeIndex].writtenRows.end());
    }
    storage.MarkComponentRowsModified(transformId, state.transformSetManyRowsScratch);
    for (std::size_t rangeIndex = 0U; rangeIndex < rangeCount; ++rangeIndex) {
        for (const SceneEntity entity : ranges[rangeIndex].linked) EnqueueSceneTransformDirtyFrontier(state, entity);
        for (const SceneEntity entity : ranges[rangeIndex].prefabNodes) MarkScenePrefabNodeDirty(state, entity);
    }
    // A live entity without a transform row gets one the structural way, after the rows above were flagged.
    for (std::size_t rangeIndex = 0U; rangeIndex < rangeCount; ++rangeIndex) {
        for (const std::size_t index : ranges[rangeIndex].unstored) set(index);
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

void SceneTransformService::MarkParentModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Transforms().MarkParentModified(entity);
        EnqueueSceneTransformDirtyFrontier(state, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

} // namespace kb::scene
