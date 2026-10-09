#include "scene/transform/SceneTransformLeafBatchUpdater.hpp"

#include "scene/SceneState.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"

namespace kb::scene {
namespace {
const TransformComponent* NativeTransform(const SceneState& state, SceneEntity entity) noexcept {
    const auto& storage = state.world.NativeStorage();
    return static_cast<const TransformComponent*>(storage.TryGetComponentData(entity, state.components.TransformComponentId()));
}
} // namespace

bool SceneTransformLeafBatchUpdater::CanUpdate(SceneEntity entity) noexcept {
    if (SceneHierarchyCache::ChildCount(state_, entity) != 0U) return false;
    ResolveParent(entity);
    if (parentChecked_) return parentsReady_;
    parentChecked_ = true;
    parentsReady_ = false;
    auto parent = parent_;
    // A clean immediate parent may still wait for a dirty ancestor.
    std::size_t remaining = state_.transformTopology.Levels().size();
    while (parent.IsValid()) {
        if (remaining-- == 0U) return false;
        const auto* transform = parent == parent_ ? parentTransform_ : NativeTransform(state_, parent);
        if (transform == nullptr || transform->worldDirty) return false;
        parent = SceneHierarchyCache::Parent(state_, parent);
    }
    parentsReady_ = true;
    return true;
}

void SceneTransformLeafBatchUpdater::ResolveParent(SceneEntity entity) noexcept {
    const auto parent = SceneHierarchyCache::Parent(state_, entity);
    if (!parentResolved_ || parent != parent_) {
        parent_ = parent;
        parentTransform_ = parent.IsValid() ? NativeTransform(state_, parent) : nullptr;
        parentResolved_ = true;
        parentChecked_ = false;
    }
}

SceneTransformBatchEntry SceneTransformLeafBatchUpdater::Update(SceneEntity entity, TransformComponent& transform) noexcept {
    ResolveParent(entity);
    SceneTransformBatchEntry entry{
        .entity = entity, .transform = &transform,
        .parentTransform = parentTransform_ == nullptr ? TransformComponent{} : *parentTransform_,
        .state = &state_, .parentEntity = parent_,
        .hasParent = parent_.IsValid(), .parentWorldVersion = parentTransform_ == nullptr ? 0U : parentTransform_->worldVersion,
    };
    UpdateSceneTransformBatchEntry(entry);
    return entry;
}

} // namespace kb::scene
