#include "engine/scene/SceneVisibilityResolution.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/VisibilityComponent.hpp"

namespace kb::scene {

ResolvedVisibility ResolveVisibility(const Scene& scene, SceneEntity entity) noexcept {
    ResolvedVisibility resolved{};
    SceneEntity current = entity;
    bool visible = true;
    bool visibilityChosen = false;

    while (current.IsValid()) {
        const VisibilityComponent* component = scene.Components().Visibility().TryGet(current);
        if (component != nullptr) {
            resolved.mask &= component->mask;
            if (!visibilityChosen && !component->visible) {
                visible = false;
                visibilityChosen = true;
            } else if (!visibilityChosen && component->mode != VisibilityMode::Inherit) {
                visible = component->mode == VisibilityMode::Visible;
                visibilityChosen = true;
            }
        }
        current = scene.Hierarchy().Parent(current);
    }

    resolved.visible = visible;
    return resolved;
}

} // namespace kb::scene
