#pragma once

#include "engine/scene/Scene.hpp"

#include <cstdint>
#include <vector>

namespace kb::editor {

class EditorSceneContext {
public:
    struct HierarchyRow {
        kb::scene::SceneEntity entity{};
        std::uint32_t depth = 0;
    };

    EditorSceneContext();

    [[nodiscard]] kb::scene::Scene& Scene() noexcept;
    [[nodiscard]] const kb::scene::Scene& Scene() const noexcept;

    [[nodiscard]] kb::scene::SceneEntity SelectedEntity() const noexcept;
    void SelectEntity(kb::scene::SceneEntity entity) noexcept;
    [[nodiscard]] bool SelectHierarchyRow(std::size_t rowIndex) noexcept;

    [[nodiscard]] std::vector<HierarchyRow> HierarchyRows() const;

private:
    void AppendHierarchyRows(kb::scene::SceneEntity entity, std::uint32_t depth, std::vector<HierarchyRow>& rows) const;

    kb::scene::Scene scene_;
    kb::scene::SceneEntity selectedEntity_{};
};

} // namespace kb::editor
