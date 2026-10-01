#pragma once

#include "scene/transform/SceneTransformBranchUpdater.hpp"

namespace kb::scene {

class SceneState;

// The native rows keep ownership. Only independent leaves whose ancestors
// are already synchronized may bypass the hierarchy's temporary value cache.
// Each instance borrows parents only for one validation pass or native range;
// callbacks and structural changes run after all worker ranges have joined.
class SceneTransformLeafBatchUpdater {
public:
    explicit SceneTransformLeafBatchUpdater(const SceneState& state) noexcept : state_(state) {}
    // Called only for a live, dirty row borrowed from the native query.
    [[nodiscard]] bool CanUpdate(SceneEntity entity) noexcept;
    [[nodiscard]] SceneTransformBatchEntry Update(SceneEntity entity, TransformComponent& transform) noexcept;

private:
    void ResolveParent(SceneEntity entity) noexcept;
    const SceneState& state_;
    SceneEntity parent_;
    const TransformComponent* parentTransform_ = nullptr;
    bool parentResolved_ = false;
    bool parentChecked_ = false;
    bool parentsReady_ = false;
};

} // namespace kb::scene
