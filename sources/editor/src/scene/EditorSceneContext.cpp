#include "scene/EditorSceneContext.hpp"

#include "scene/EditorDefaultSceneFactory.hpp"

namespace kb::editor {

EditorSceneContext::EditorSceneContext() {
    selectedEntity_ = EditorDefaultSceneFactory::Seed(scene_);
}

kb::scene::Scene& EditorSceneContext::Scene() noexcept {
    return scene_;
}

const kb::scene::Scene& EditorSceneContext::Scene() const noexcept {
    return scene_;
}

kb::scene::SceneEntity EditorSceneContext::SelectedEntity() const noexcept {
    return selectedEntity_;
}

void EditorSceneContext::SelectEntity(kb::scene::SceneEntity entity) noexcept {
    selectedEntity_ = scene_.IsAlive(entity) ? entity : kb::scene::SceneEntity{};
}

bool EditorSceneContext::SelectHierarchyRow(std::size_t rowIndex) noexcept {
    const std::vector<HierarchyRow> rows = HierarchyRows();
    if (rowIndex >= rows.size()) {
        selectedEntity_ = {};
        return false;
    }

    SelectEntity(rows[rowIndex].entity);
    return selectedEntity_.IsValid();
}

std::vector<EditorSceneContext::HierarchyRow> EditorSceneContext::HierarchyRows() const {
    std::vector<HierarchyRow> rows;
    for (const kb::scene::SceneEntity root : scene_.RootEntities()) {
        AppendHierarchyRows(root, 0, rows);
    }
    return rows;
}

void EditorSceneContext::AppendHierarchyRows(kb::scene::SceneEntity entity, std::uint32_t depth, std::vector<HierarchyRow>& rows) const {
    rows.push_back(HierarchyRow{
        .entity = entity,
        .depth = depth,
    });

    for (const kb::scene::SceneEntity child : scene_.ChildEntities(entity)) {
        AppendHierarchyRows(child, depth + 1U, rows);
    }
}

} // namespace kb::editor
