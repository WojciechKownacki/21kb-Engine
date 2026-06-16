#include "scene/transform/SceneTransformBranchUpdater.hpp"

#include "scene/transform/TransformMath.hpp"

#include <algorithm>

namespace kb::scene {
namespace {

void UpdateEntry(SceneTransformBatchEntry& entry) noexcept {
    if (entry.transform == nullptr) {
        return;
    }

    const bool shouldUpdate = entry.parentDirty || entry.transform->worldDirty;
    if (shouldUpdate) {
        *entry.transform = TransformMath::Compose(entry.parentTransform, *entry.transform);
    }
    entry.updated = shouldUpdate;
}

void UpdateEntries(std::span<SceneTransformBatchEntry> entries) noexcept {
    for (SceneTransformBatchEntry& entry : entries) {
        UpdateEntry(entry);
    }
}

} // namespace

void SceneTransformBranchUpdater::UpdateBatch(kb::ecs::WorkerPool* workerPool, std::span<SceneTransformBatchEntry> entries, std::size_t grainSize) const {
    if (entries.empty()) {
        return;
    }
    if (workerPool == nullptr || !workerPool->Running() || entries.size() <= grainSize) {
        UpdateEntries(entries);
        return;
    }

    workerPool->ParallelForChunks(entries.size(), std::max<std::size_t>(1U, grainSize), [&entries](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
        UpdateEntries(entries.subspan(chunk.begin, chunk.count));
    });
}

} // namespace kb::scene
