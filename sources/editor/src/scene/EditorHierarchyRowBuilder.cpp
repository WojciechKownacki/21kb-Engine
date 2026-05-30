#include "scene/EditorHierarchyRowBuilder.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "scene/EditorHierarchyRowFactory.hpp"
#include "scene/EditorHierarchySearchMatcher.hpp"

#include <string>

namespace kb::editor {

std::vector<EditorHierarchyRow> EditorHierarchyRowBuilder::Build(const kb::scene::Scene& scene, const CollapsedEntitySet& collapsedEntities, std::string_view searchQuery) {
    std::vector<EditorHierarchyRow> rows;
    const std::string normalizedQuery = EditorHierarchySearchMatcher::Normalize(searchQuery);

    for (const kb::scene::SceneEntity root : scene.Hierarchy().RootEntities()) {
        if (normalizedQuery.empty()) {
            Append(scene, collapsedEntities, root, 0, rows);
        } else {
            static_cast<void>(AppendFiltered(scene, root, 0, normalizedQuery, rows));
        }
    }
    return rows;
}

void EditorHierarchyRowBuilder::Append(const kb::scene::Scene& scene, const CollapsedEntitySet& collapsedEntities, kb::scene::SceneEntity entity, std::uint32_t depth, std::vector<EditorHierarchyRow>& rows) {
    const EditorHierarchyRow row = EditorHierarchyRowFactory::Make(scene, collapsedEntities, entity, depth);
    rows.push_back(row);
    if (!row.expanded) {
        return;
    }

    for (const kb::scene::SceneEntity child : scene.Hierarchy().ChildEntities(entity)) {
        Append(scene, collapsedEntities, child, depth + 1U, rows);
    }
}

bool EditorHierarchyRowBuilder::AppendFiltered(const kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::uint32_t depth, std::string_view normalizedQuery, std::vector<EditorHierarchyRow>& rows) {
    std::vector<EditorHierarchyRow> childRows;
    for (const kb::scene::SceneEntity child : scene.Hierarchy().ChildEntities(entity)) {
        static_cast<void>(AppendFiltered(scene, child, depth + 1U, normalizedQuery, childRows));
    }

    const std::string name = scene.Entities().Name(entity);
    const bool selfMatches = EditorHierarchySearchMatcher::Matches(name, normalizedQuery);
    if (!selfMatches && childRows.empty()) {
        return false;
    }

    EditorHierarchyRow row = EditorHierarchyRowFactory::Make(scene, CollapsedEntitySet{}, entity, depth);
    row.name = name;
    row.expanded = !childRows.empty();
    rows.push_back(std::move(row));
    rows.insert(rows.end(), childRows.begin(), childRows.end());
    return true;
}

} // namespace kb::editor
