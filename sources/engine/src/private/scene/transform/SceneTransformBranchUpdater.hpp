#pragma once

#include "engine/ecs/WorkerPool.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cstddef>
#include <span>

namespace kb::scene {

struct SceneTransformBatchEntry {
    SceneEntity entity{};
    TransformComponent* transform = nullptr;
    TransformComponent parentTransform{};
    bool hasParent = false;
    bool parentDirty = false;
    std::uint64_t parentWorldVersion = 0;
    bool updated = false;
    bool rootFastPath = false;
    bool translatedParentFastPath = false;
    bool unrotatedParentFastPath = false;
    bool unitScaleParentFastPath = false;
    bool uniformScaleParentFastPath = false;
    bool staticLocalRotationFastPath = false;
};

struct SceneTransformHierarchyKernelContract {
    using Entry = SceneTransformBatchEntry;
};

void UpdateSceneTransformBatchEntry(SceneTransformBatchEntry& entry) noexcept;

class SceneTransformKernelBatch {
public:
    explicit SceneTransformKernelBatch(std::span<SceneTransformBatchEntry> entries) noexcept;

    [[nodiscard]] std::size_t Count() const noexcept;
    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] SceneTransformBatchEntry& EntryAt(std::size_t index) const noexcept;
    [[nodiscard]] std::span<SceneTransformBatchEntry> Entries() const noexcept;

private:
    std::span<SceneTransformBatchEntry> entries_;
};

class SceneTransformHierarchyKernel {
public:
    void operator()(SceneTransformKernelBatch& batch) const noexcept;
};

class SceneTransformBranchUpdater {
public:
    void UpdateBatch(kb::ecs::WorkerPool* workerPool, std::span<SceneTransformBatchEntry> entries, std::size_t grainSize) const;
};

} // namespace kb::scene
