#include "scene/hierarchy/SceneHierarchyParentAssignmentService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/SceneTransformService.hpp"
#include "scene/hierarchy/SceneHierarchyOperations.hpp"

namespace kb::scene {

bool SceneHierarchyParentAssignmentService::SetParent(Scene& scene, SceneObject child, SceneObject parent) noexcept {
    if (!SceneAccess::BelongsTo(scene, child)) {
        return false;
    }
    if (parent.Entity().IsValid() && !SceneAccess::BelongsTo(scene, parent)) {
        return false;
    }

    return SetParent(scene, child.Entity(), parent.Entity());
}

bool SceneHierarchyParentAssignmentService::SetParent(Scene& scene, SceneEntity child, SceneEntity parent) noexcept {
    const bool changed = SceneHierarchyOperations::SetParent(SceneAccess::State(scene).world, child, parent);
    if (changed) {
        SceneTransformService::MarkModified(scene, child);
    }
    return changed;
}

} // namespace kb::scene
