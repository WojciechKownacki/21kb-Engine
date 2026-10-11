#include "settings/EditorLayoutMenuModel.hpp"

#include "localization/EditorLocalization.hpp"
#include "settings/EditorLayoutLibrary.hpp"

namespace kb::editor {

void EditorLayoutMenuModel::Rebuild(
    const std::vector<std::string>& savedLayouts, std::string_view activeLayout) {
    rows_.clear();
    rows_.push_back({
        .label = std::string{ EditorText("layout.default", "Default") },
        .action = EditorLayoutMenuAction::Default,
        .active = activeLayout.empty(),
    });
    for (const std::string& name : savedLayouts) {
        if (rows_.size() + 2U >= MaximumRows) {
            break;
        }
        rows_.push_back({
            .label = name,
            .action = EditorLayoutMenuAction::Apply,
            .layoutName = name,
            .active = name == activeLayout,
        });
    }
    rows_.push_back({ .label = std::string{ EditorText("layout.saveRow", "Save Layout...") }, .action = EditorLayoutMenuAction::Save });
    rows_.push_back({ .label = std::string{ EditorText("layout.deleteRow", "Delete Layout...") }, .action = EditorLayoutMenuAction::Delete });
}

void EditorLayoutMenuModel::RebuildForDelete(const std::vector<std::string>& savedLayouts) {
    rows_.clear();
    rows_.push_back({
        .label = std::string{ savedLayouts.empty()
            ? EditorText("layout.noSavedLayouts", "No saved layouts")
            : EditorText("layout.deleteWhich", "Delete which layout?") },
        .action = EditorLayoutMenuAction::Delete,
        .enabled = false,
    });
    for (const std::string& name : savedLayouts) {
        if (rows_.size() + 2U >= MaximumRows) {
            break;
        }
        rows_.push_back({
            .label = name,
            .action = EditorLayoutMenuAction::Remove,
            .layoutName = name,
        });
    }
}

const std::vector<EditorLayoutMenuRow>& EditorLayoutMenuModel::Rows() const noexcept {
    return rows_;
}

const EditorLayoutMenuRow* EditorLayoutMenuModel::Row(int index) const noexcept {
    if (index < 0 || static_cast<std::size_t>(index) >= rows_.size()) {
        return nullptr;
    }
    return &rows_[static_cast<std::size_t>(index)];
}

} // namespace kb::editor
