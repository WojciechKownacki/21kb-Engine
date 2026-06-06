#include "scene/EditorHierarchySelectionState.hpp"

#include <algorithm>

namespace kb::editor {

kb::scene::SceneEntity EditorHierarchySelectionState::Primary() const noexcept {
    return primary_;
}

const std::vector<kb::scene::SceneEntity>& EditorHierarchySelectionState::SelectedEntities() const noexcept {
    return selected_;
}

bool EditorHierarchySelectionState::IsSelected(kb::scene::SceneEntity entity) const noexcept {
    return std::ranges::find(selected_, entity) != selected_.end();
}

void EditorHierarchySelectionState::SelectEntity(kb::scene::SceneEntity entity) {
    primary_ = entity;
    selected_.clear();
    if (entity.IsValid()) {
        selected_.push_back(entity);
        anchor_ = entity;
    } else {
        anchor_ = {};
    }
}

void EditorHierarchySelectionState::Clear() noexcept {
    primary_ = {};
    selected_.clear();
    anchor_ = {};
}

bool EditorHierarchySelectionState::SelectRow(const std::vector<EditorHierarchyRow>& rows, std::size_t rowIndex, bool additive, bool range) {
    if (rowIndex >= rows.size()) {
        if (!additive && !range) {
            Clear();
        }
        return false;
    }

    const kb::scene::SceneEntity clicked = rows[rowIndex].entity;
    if (!clicked.IsValid()) {
        return false;
    }

    if (range) {
        std::size_t anchorIndex = rowIndex;
        if (anchor_.IsValid()) {
            for (std::size_t index = 0; index < rows.size(); ++index) {
                if (rows[index].entity == anchor_) {
                    anchorIndex = index;
                    break;
                }
            }
        }

        if (!additive) {
            selected_.clear();
        }

        const std::size_t first = std::min(anchorIndex, rowIndex);
        const std::size_t last = std::max(anchorIndex, rowIndex);
        for (std::size_t index = first; index <= last; ++index) {
            const kb::scene::SceneEntity entity = rows[index].entity;
            if (!IsSelected(entity)) {
                selected_.push_back(entity);
            }
        }
        primary_ = clicked;
        return true;
    }

    if (additive) {
        const auto existing = std::ranges::find(selected_, clicked);
        if (existing != selected_.end()) {
            selected_.erase(existing);
            primary_ = selected_.empty() ? kb::scene::SceneEntity{} : selected_.back();
        } else {
            selected_.push_back(clicked);
            primary_ = clicked;
        }
        anchor_ = clicked;
        return true;
    }

    if (selected_.size() > 1U && IsSelected(clicked)) {
        primary_ = clicked;
        anchor_ = clicked;
        return true;
    }

    SelectEntity(clicked);
    return primary_.IsValid();
}

} // namespace kb::editor
