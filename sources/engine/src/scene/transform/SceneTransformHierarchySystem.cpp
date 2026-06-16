#include "scene/transform/SceneTransformHierarchySystem.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentRegistry.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"
#include "scene/transform/SceneTransformBranchUpdater.hpp"
#include "scene/transform/TransformMath.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>

namespace kb::scene {
namespace {

inline constexpr std::size_t kTransformBatchGrainSize = 128U;

void BuildTopologicalBatches(SceneState& state) {
    state.transformTopologicalBatches.clear();
    if (state.hierarchyRoots.empty()) {
        return;
    }

    state.transformTopologicalBatches.push_back(state.hierarchyRoots);
    for (std::size_t levelIndex = 0; levelIndex < state.transformTopologicalBatches.size(); ++levelIndex) {
        const std::vector<SceneEntity>& level = state.transformTopologicalBatches[levelIndex];
        std::vector<SceneEntity>* nextLevel = nullptr;
        for (const SceneEntity entity : level) {
            const auto children = state.hierarchyChildren.find(entity.Id());
            if (children == state.hierarchyChildren.end() || children->second.empty()) {
                continue;
            }
            if (nextLevel == nullptr) {
                if (levelIndex + 1U == state.transformTopologicalBatches.size()) {
                    state.transformTopologicalBatches.emplace_back();
                }
                nextLevel = &state.transformTopologicalBatches[levelIndex + 1U];
            }
            nextLevel->insert(nextLevel->end(), children->second.begin(), children->second.end());
        }
    }
}

[[nodiscard]] SceneEntity ParentOf(const SceneState& state, SceneEntity entity) noexcept {
    const auto parent = state.hierarchyParents.find(entity.Id());
    return parent == state.hierarchyParents.end() ? SceneEntity{} : parent->second;
}

[[nodiscard]] TransformComponent ParentTransformOf(const SceneState& state, SceneEntity parent, const TransformComponent& identity) {
    if (!parent.IsValid()) {
        return identity;
    }
    const auto transform = state.transformWorldScratch.find(parent.Id());
    return transform == state.transformWorldScratch.end() ? identity : transform->second;
}

[[nodiscard]] std::uint64_t ParentWorldVersionOf(const SceneState& state, SceneEntity parent) noexcept {
    if (!parent.IsValid()) {
        return 0;
    }
    const auto transform = state.transformWorldScratch.find(parent.Id());
    return transform == state.transformWorldScratch.end() ? 0 : transform->second.worldVersion;
}

[[nodiscard]] bool ParentDirtyOf(const SceneState& state, SceneEntity parent) noexcept {
    if (!parent.IsValid()) {
        return false;
    }
    const auto dirty = state.transformDirtyScratch.find(parent.Id());
    return dirty != state.transformDirtyScratch.end() && dirty->second;
}

void EnsureWorkerPool(SceneState& state) {
    if (state.transformWorkerPool == nullptr) {
        state.transformWorkerPool = std::make_unique<kb::ecs::WorkerPool>(kb::ecs::WorkerPoolConfig{});
    } else if (!state.transformWorkerPool->Running()) {
        state.transformWorkerPool->Start(kb::ecs::WorkerPoolConfig{});
    }
}

void CacheRenderProxyUpdatesAfterTransforms(SceneState& state, std::span<const SceneEntity> updatedEntities) {
    if (updatedEntities.empty()) {
        return;
    }

    const std::size_t writeBegin = state.transformRenderProxyUpdateEntities.size();
    state.transformRenderProxyUpdateEntities.resize(writeBegin + updatedEntities.size());
    if (updatedEntities.size() <= kTransformBatchGrainSize) {
        std::ranges::copy(updatedEntities, state.transformRenderProxyUpdateEntities.begin() + static_cast<std::ptrdiff_t>(writeBegin));
        return;
    }

    EnsureWorkerPool(state);
    state.transformWorkerPool->ParallelForChunks(updatedEntities.size(), kTransformBatchGrainSize, [&state, updatedEntities, writeBegin](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
        for (std::size_t offset = 0; offset < chunk.count; ++offset) {
            state.transformRenderProxyUpdateEntities[writeBegin + chunk.begin + offset] = updatedEntities[chunk.begin + offset];
        }
    });
}

} // namespace

void SceneTransformHierarchySystem::Update(SceneState& state) const {
    const TransformComponent identity = TransformMath::Identity();
    BuildTopologicalBatches(state);
    state.transformDirtyScratch.clear();
    state.transformWorldScratch.clear();
    state.transformDirtyScratch.reserve(state.hierarchyOrder.size());
    state.transformWorldScratch.reserve(state.hierarchyOrder.size());

    std::vector<SceneTransformBatchEntry> entries;
    std::vector<SceneEntity> updatedEntities;
    updatedEntities.reserve(state.hierarchyOrder.size());
    for (const std::vector<SceneEntity>& level : state.transformTopologicalBatches) {
        entries.clear();
        entries.reserve(level.size());

        for (const SceneEntity entity : level) {
            TransformComponent* transform = SceneComponentStorageAccess::TryGetMutable<TransformComponent>(state.world.NativeHandle(), entity, state.components.TransformComponentId());
            if (transform == nullptr) {
                continue;
            }

            const SceneEntity parent = ParentOf(state, entity);
            entries.push_back(SceneTransformBatchEntry{
                .entity = entity,
                .transform = transform,
                .parentTransform = ParentTransformOf(state, parent, identity),
                .parentDirty = ParentDirtyOf(state, parent),
                .parentWorldVersion = ParentWorldVersionOf(state, parent),
            });
        }

        if (entries.empty()) {
            continue;
        }

        kb::ecs::WorkerPool* workerPool = nullptr;
        if (entries.size() > kTransformBatchGrainSize) {
            EnsureWorkerPool(state);
            workerPool = state.transformWorkerPool.get();
        }
        SceneTransformBranchUpdater{}.UpdateBatch(workerPool, entries, kTransformBatchGrainSize);

        for (const SceneTransformBatchEntry& entry : entries) {
            state.transformWorldScratch[entry.entity.Id()] = *entry.transform;
            state.transformDirtyScratch[entry.entity.Id()] = entry.updated;
            if (entry.updated) {
                updatedEntities.push_back(entry.entity);
                SceneComponentAccess::MarkModified(state.world.NativeHandle(), entry.entity, state.components.TransformComponentId());
            }
        }
    }

    CacheRenderProxyUpdatesAfterTransforms(state, updatedEntities);
}

} // namespace kb::scene
