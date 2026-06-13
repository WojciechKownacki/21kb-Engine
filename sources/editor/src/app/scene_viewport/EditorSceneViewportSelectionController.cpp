#include "app/scene_viewport/EditorSceneViewportSelectionController.hpp"

#if defined(_WIN32)
#include <algorithm>
#include <vector>

namespace kb::editor {
namespace {

[[nodiscard]] bool Contains(std::span<const kb::scene::SceneEntity> entities, kb::scene::SceneEntity entity) noexcept {
    return std::ranges::find(entities, entity) != entities.end();
}

} // namespace

void EditorSceneViewportSelectionController::ApplyClick(
    EditorSceneContext& sceneContext,
    kb::scene::SceneEntity entity,
    EditorSceneViewportSelectionMode mode) {
    if (mode == EditorSceneViewportSelectionMode::Replace) {
        if (entity.IsValid()) {
            sceneContext.SelectEntity(entity);
        } else {
            sceneContext.ClearHierarchySelection();
        }
        return;
    }

    std::vector<kb::scene::SceneEntity> selected = sceneContext.SelectedHierarchyEntities();
    const auto existing = std::ranges::find(selected, entity);
    if (entity.IsValid() && existing == selected.end()) {
        selected.push_back(entity);
    } else if (existing != selected.end()) {
        selected.erase(existing);
    }

    if (selected.empty()) {
        sceneContext.ClearHierarchySelection();
    } else {
        sceneContext.SelectHierarchyEntities(selected);
    }
}

void EditorSceneViewportSelectionController::ApplyBox(
    EditorSceneContext& sceneContext,
    std::span<const kb::scene::SceneEntity> entities,
    EditorSceneViewportSelectionMode mode) {
    if (mode == EditorSceneViewportSelectionMode::Replace) {
        if (entities.empty()) {
            sceneContext.ClearHierarchySelection();
        } else {
            sceneContext.SelectHierarchyEntities(entities);
        }
        return;
    }

    std::vector<kb::scene::SceneEntity> selected = sceneContext.SelectedHierarchyEntities();
    for (const kb::scene::SceneEntity entity : entities) {
        if (entity.IsValid() && !Contains(selected, entity)) {
            selected.push_back(entity);
        }
    }

    if (!selected.empty()) {
        sceneContext.SelectHierarchyEntities(selected);
    }
}

} // namespace kb::editor

#endif
