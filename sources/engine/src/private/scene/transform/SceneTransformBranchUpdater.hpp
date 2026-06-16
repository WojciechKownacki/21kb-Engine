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
    bool parentDirty = false;
    bool updated = false;
};

class SceneTransformBranchUpdater {
public:
    void UpdateBatch(kb::ecs::WorkerPool* workerPool, std::span<SceneTransformBatchEntry> entries, std::size_t grainSize) const;
};

} // namespace kb::scene
