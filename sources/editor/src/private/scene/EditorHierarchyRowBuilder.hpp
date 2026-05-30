#pragma once

#include "scene/EditorHierarchyRow.hpp"

#include <string_view>
#include <unordered_set>
#include <vector>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::editor {

class EditorHierarchyRowBuilder {
public:
    using CollapsedEntitySet = std::unordered_set<kb::scene::SceneEntity::IdType>;

    [[nodiscard]] static std::vector<EditorHierarchyRow> Build(const kb::scene::Scene& scene, const CollapsedEntitySet& collapsedEntities, std::string_view searchQuery);

private:
    static void Append(const kb::scene::Scene& scene, const CollapsedEntitySet& collapsedEntities, kb::scene::SceneEntity entity, std::uint32_t depth, std::vector<EditorHierarchyRow>& rows);
    [[nodiscard]] static bool AppendFiltered(const kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::uint32_t depth, std::string_view normalizedQuery, std::vector<EditorHierarchyRow>& rows);
};

} // namespace kb::editor
