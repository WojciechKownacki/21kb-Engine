#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "scene/EditorHierarchyRow.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace kb::editor {

class EditorHierarchySelectionState {
public:
    [[nodiscard]] kb::scene::SceneEntity Primary() const noexcept;
    [[nodiscard]] const std::vector<kb::scene::SceneEntity>& SelectedEntities() const noexcept;
    [[nodiscard]] bool IsSelected(kb::scene::SceneEntity entity) const noexcept;

    void SelectEntity(kb::scene::SceneEntity entity);
    void SelectEntities(std::span<const kb::scene::SceneEntity> entities);
    void Clear() noexcept;
    [[nodiscard]] bool SelectRow(const std::vector<EditorHierarchyRow>& rows, std::size_t rowIndex, bool additive, bool range);

private:
    kb::scene::SceneEntity primary_{};
    std::vector<kb::scene::SceneEntity> selected_;
    kb::scene::SceneEntity anchor_{};
};

} // namespace kb::editor
