#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneCameraComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneVisibilityResolution.hpp"

#include <cstddef>

namespace kb::editor {

class EditorPlayCameraResolver final {
public:
    [[nodiscard]] static kb::scene::SceneEntity Resolve(const kb::scene::Scene& scene) {
        kb::scene::SceneEntity selected{};
        for (const kb::scene::SceneEntity root : scene.Hierarchy().RootEntities()) {
            ResolveBranch(scene, root, selected);
        }
        return selected;
    }

private:
    static void ResolveBranch(
        const kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        kb::scene::SceneEntity& selected) {
        const kb::scene::CameraComponent* camera =
            scene.Components().Cameras().TryGet(entity);
        if (camera != nullptr && camera->primary &&
            kb::scene::ResolveVisibility(scene, entity).visible) {
            // Hierarchy is traversed in the exact root/child order shown by
            // the editor. Replacing the candidate makes the last active row
            // the deterministic Play camera.
            selected = entity;
        }
        const std::size_t childCount = scene.Hierarchy().ChildCount(entity);
        for (std::size_t index = 0U; index < childCount; ++index) {
            ResolveBranch(scene, scene.Hierarchy().ChildAt(entity, index), selected);
        }
    }
};

} // namespace kb::editor
