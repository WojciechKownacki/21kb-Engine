#include "app/scene_viewport/EditorSceneViewportSelectionController.hpp"

#if defined(_WIN32)
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include <algorithm>
#include <vector>

namespace kb::editor {
namespace {

kb::scene::SceneEntity AuthoredEntity(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    auto result = entity;
    for (auto ancestor = entity; ancestor.IsValid(); ancestor = scene.Hierarchy().Parent(ancestor)) {
        if (const auto* renderer = scene.Components().MeshRenderers().TryGet(ancestor); renderer && renderer->transient) result = scene.Hierarchy().Parent(ancestor);
    }
    return result;
}

[[nodiscard]] bool Contains(std::span<const kb::scene::SceneEntity> entities, kb::scene::SceneEntity entity) noexcept {
    return std::ranges::find(entities, entity) != entities.end();
}

} // namespace

void EditorSceneViewportSelectionController::ApplyClick(
    EditorSceneContext& sceneContext,
    kb::scene::SceneEntity entity,
    EditorSceneViewportSelectionMode mode) {
    entity = AuthoredEntity(sceneContext.Scene(), entity);
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
    std::vector<kb::scene::SceneEntity> authored;
    for (const auto entity : entities) {
        const auto owner = AuthoredEntity(sceneContext.Scene(), entity);
        if (owner.IsValid() && std::ranges::find(authored, owner) == authored.end()) authored.push_back(owner);
    }
    entities = authored;
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
        if (!entity.IsValid()) {
            continue;
        }
        const auto existing = std::ranges::find(selected, entity);
        if (existing == selected.end()) {
            selected.push_back(entity);
        } else {
            selected.erase(existing);
        }
    }

    if (selected.empty()) {
        sceneContext.ClearHierarchySelection();
    } else {
        sceneContext.SelectHierarchyEntities(selected);
    }
}

} // namespace kb::editor

#endif
