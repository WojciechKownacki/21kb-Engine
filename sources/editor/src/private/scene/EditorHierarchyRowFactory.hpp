#pragma once

#include "scene/EditorHierarchyRow.hpp"
#include "scene/EditorHierarchyRowBuilder.hpp"

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::editor {

class EditorHierarchyRowFactory {
public:
    EditorHierarchyRowFactory() = delete;

    [[nodiscard]] static EditorHierarchyRow Make(
        const kb::scene::Scene& scene,
        const EditorHierarchyRowBuilder::CollapsedEntitySet& collapsedEntities,
        kb::scene::SceneEntity entity,
        std::uint32_t depth);
};

} // namespace kb::editor
