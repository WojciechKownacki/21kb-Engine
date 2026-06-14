#include "scene/EditorHierarchySelectionNormalizer.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "scene/EditorHierarchySelectionState.hpp"

#include <algorithm>
#include <vector>

namespace kb::editor {
namespace {

[[nodiscard]] bool Contains(std::span<const kb::scene::SceneEntity> entities, kb::scene::SceneEntity entity) noexcept {
    return std::ranges::find(entities, entity) != entities.end();
}

} // namespace

void EditorHierarchySelectionNormalizer::NormalizeAfterSceneRestore(
    const kb::scene::Scene& scene,
    EditorHierarchySelectionState& selection,
    std::span<const EditorHierarchyRow> visibleRows) {
    const kb::scene::SceneEntity previousPrimary = selection.Primary();
    const std::vector<kb::scene::SceneEntity> previous = selection.SelectedEntities();

    std::vector<kb::scene::SceneEntity> alive;
    alive.reserve(previous.size());
    for (const kb::scene::SceneEntity entity : previous) {
        if (entity.IsValid() && scene.Entities().IsAlive(entity) && !Contains(alive, entity)) {
            alive.push_back(entity);
        }
    }

    if (!alive.empty()) {
        if (previousPrimary.IsValid() && scene.Entities().IsAlive(previousPrimary)) {
            const auto primary = std::ranges::find(alive, previousPrimary);
            if (primary != alive.end() && primary + 1 != alive.end()) {
                const kb::scene::SceneEntity value = *primary;
                alive.erase(primary);
                alive.push_back(value);
            }
        }
        selection.SelectEntities(alive);
        return;
    }

    for (const EditorHierarchyRow& row : visibleRows) {
        if (row.entity.IsValid() && scene.Entities().IsAlive(row.entity)) {
            selection.SelectEntity(row.entity);
            return;
        }
    }

    selection.Clear();
}

} // namespace kb::editor
