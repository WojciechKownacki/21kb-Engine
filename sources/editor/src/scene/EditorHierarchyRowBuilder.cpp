#include "scene/EditorHierarchyRowBuilder.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "scene/EditorHierarchyRowFactory.hpp"
#include "scene/EditorHierarchySearchMatcher.hpp"

#include <algorithm>
#include <string>

namespace kb::editor {

bool EditorHierarchyRowBuilder::IsTransient(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    const auto* renderer = scene.Components().MeshRenderers().TryGet(entity);
    // Only the const query is missing from the const scene; reading the flag changes nothing.
    return (renderer != nullptr && renderer->transient) ||
        kb::scene::SceneEntities{ const_cast<kb::scene::Scene&>(scene) }.IsTransient(entity);
}

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

std::vector<EditorHierarchyRow> EditorHierarchyRowBuilder::BuildLeading(const kb::scene::Scene& scene, const CollapsedEntitySet& collapsedEntities, std::string_view searchQuery, std::size_t limit) {
    if (!searchQuery.empty()) {
        std::vector<EditorHierarchyRow> rows = Build(scene, collapsedEntities, searchQuery);
        rows.resize(std::min(rows.size(), limit));
        return rows;
    }
    std::vector<EditorHierarchyRow> rows;
    const kb::scene::SceneHierarchyQueries hierarchy = scene.Hierarchy();
    for (std::size_t index = 0U; index < hierarchy.RootCount() && rows.size() < limit; ++index) {
        Append(scene, collapsedEntities, hierarchy.RootAt(index), 0U, rows, limit);
    }
    return rows;
}

void EditorHierarchyRowBuilder::AppendRoot(const kb::scene::Scene& scene,
    const CollapsedEntitySet& collapsedEntities, kb::scene::SceneEntity root,
    std::vector<EditorHierarchyRow>& rows) {
    Append(scene, collapsedEntities, root, 0U, rows);
}

void EditorHierarchyRowBuilder::Append(const kb::scene::Scene& scene, const CollapsedEntitySet& collapsedEntities, kb::scene::SceneEntity entity, std::uint32_t depth, std::vector<EditorHierarchyRow>& rows, std::size_t limit) {
    if (IsTransient(scene, entity)) return;
    const EditorHierarchyRow row = EditorHierarchyRowFactory::Make(scene, collapsedEntities, entity, depth);
    rows.push_back(row);
    if (!row.expanded) {
        return;
    }

    const kb::scene::SceneHierarchyQueries hierarchy = scene.Hierarchy();
    for (std::size_t index = 0U; index < hierarchy.ChildCount(entity) && rows.size() < limit; ++index) {
        Append(scene, collapsedEntities, hierarchy.ChildAt(entity, index), depth + 1U, rows, limit);
    }
}

bool EditorHierarchyRowBuilder::AppendFiltered(const kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::uint32_t depth, std::string_view normalizedQuery, std::vector<EditorHierarchyRow>& rows) {
    if (IsTransient(scene, entity)) return false;
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
