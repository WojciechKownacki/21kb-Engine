#include "scene/transform/SceneTransformBranchUpdater.hpp"

#include "scene/transform/TransformMath.hpp"

#include <algorithm>

namespace kb::scene {
namespace {

void UpdateEntry(SceneTransformBatchEntry& entry) noexcept {
    if (entry.transform == nullptr) {
        return;
    }

    const bool shouldUpdate = entry.parentDirty || entry.transform->worldDirty || entry.transform->parentVersion != entry.parentWorldVersion;
    if (shouldUpdate) {
        *entry.transform = TransformMath::Compose(entry.parentTransform, *entry.transform);
    }
    entry.updated = shouldUpdate;
}

void UpdateEntries(std::span<SceneTransformBatchEntry> entries) noexcept {
    SceneTransformKernelBatch batch{ entries };
    SceneTransformHierarchyKernel{}(batch);
}

} // namespace

SceneTransformKernelBatch::SceneTransformKernelBatch(std::span<SceneTransformBatchEntry> entries) noexcept
    : entries_(entries) {}

std::size_t SceneTransformKernelBatch::Count() const noexcept {
    return entries_.size();
}

bool SceneTransformKernelBatch::Empty() const noexcept {
    return entries_.empty();
}

SceneTransformBatchEntry& SceneTransformKernelBatch::EntryAt(std::size_t index) const noexcept {
    return entries_[index];
}

std::span<SceneTransformBatchEntry> SceneTransformKernelBatch::Entries() const noexcept {
    return entries_;
}

void SceneTransformHierarchyKernel::operator()(SceneTransformKernelBatch& batch) const noexcept {
    for (SceneTransformBatchEntry& entry : batch.Entries()) {
        UpdateEntry(entry);
    }
}

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
