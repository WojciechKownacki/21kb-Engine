#include "scene/EditorSceneContext.hpp"

#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneVisibilityComponents.hpp"

#include "scene/EditorDefaultSceneFactory.hpp"
#include "scene/EditorHierarchyObjectFactory.hpp"
#include "scene/EditorHierarchyRowBuilder.hpp"

#include <utility>

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
    selectedEntity_ = scene_.Entities().IsAlive(entity) ? entity : kb::scene::SceneEntity{};
}

bool EditorSceneContext::SelectHierarchyRow(std::size_t rowIndex) noexcept {
    const std::vector<EditorHierarchyRow> rows = HierarchyRows();
    if (rowIndex >= rows.size()) {
        selectedEntity_ = {};
        return false;
    }

    SelectEntity(rows[rowIndex].entity);
    return selectedEntity_.IsValid();
}

std::vector<EditorHierarchyRow> EditorSceneContext::HierarchyRows() const {
    return EditorHierarchyRowBuilder::Build(scene_, hierarchyExpansion_.CollapsedEntities(), hierarchySearch_.Query());
}

std::string_view EditorSceneContext::HierarchySearchQuery() const noexcept {
    return hierarchySearch_.Query();
}

bool EditorSceneContext::IsHierarchySearchFocused() const noexcept {
    return hierarchySearch_.IsFocused();
}

void EditorSceneContext::FocusHierarchySearch(bool focused) noexcept {
    hierarchySearch_.Focus(focused);
}

void EditorSceneContext::SetHierarchySearchQuery(std::string query) {
    hierarchySearch_.SetQuery(std::move(query));
}

void EditorSceneContext::AppendHierarchySearchText(wchar_t character) {
    hierarchySearch_.AppendAscii(character);
}

void EditorSceneContext::BackspaceHierarchySearch() {
    hierarchySearch_.Backspace();
}

void EditorSceneContext::ClearHierarchySearch() {
    hierarchySearch_.Clear();
}

bool EditorSceneContext::ToggleHierarchyRowExpanded(std::size_t rowIndex) {
    const std::vector<EditorHierarchyRow> rows = HierarchyRows();
    if (rowIndex >= rows.size() || !rows[rowIndex].hasChildren) {
        return false;
    }

    hierarchyExpansion_.SetExpanded(rows[rowIndex].entity, !rows[rowIndex].expanded);
    return true;
}

bool EditorSceneContext::ToggleEntityVisibility(kb::scene::SceneEntity entity) {
    if (!scene_.Entities().IsAlive(entity)) {
        return false;
    }

    kb::scene::VisibilityComponent visibility = scene_.Components().Visibility().Get(entity);
    visibility.visible = !visibility.visible;
    scene_.Components().Visibility().Set(entity, visibility);
    SelectEntity(entity);
    return true;
}

kb::scene::SceneEntity EditorSceneContext::CreateHierarchyObject() {
    const kb::scene::SceneEntity entity = EditorHierarchyObjectFactory::CreateObject(scene_);
    SelectEntity(entity);
    return entity;
}

} // namespace kb::editor
