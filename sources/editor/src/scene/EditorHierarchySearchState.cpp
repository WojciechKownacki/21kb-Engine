#include "scene/EditorHierarchySearchState.hpp"

#include <utility>

namespace kb::editor {

std::string_view EditorHierarchySearchState::Query() const noexcept {
    return query_;
}

bool EditorHierarchySearchState::IsFocused() const noexcept {
    return focused_;
}

void EditorHierarchySearchState::Focus(bool focused) noexcept {
    focused_ = focused;
    if (!focused_) {
        selectingAll_ = false;
    }
}

void EditorHierarchySearchState::SetQuery(std::string query) {
    query_ = std::move(query);
    selectingAll_ = false;
}

void EditorHierarchySearchState::AppendAscii(wchar_t character) {
    if (character >= 32 && character < 127) {
        if (selectingAll_) {
            query_.clear();
            selectingAll_ = false;
        }
        query_.push_back(static_cast<char>(character));
    }
}

void EditorHierarchySearchState::Insert(std::string_view text) {
    if (selectingAll_) {
        query_.clear();
        selectingAll_ = false;
    }
    for (const char character : text) {
        if (character >= 32 && character < 127) {
            query_.push_back(character);
        }
    }
}

void EditorHierarchySearchState::Backspace() {
    if (selectingAll_) {
        Clear();
        return;
    }
    if (!query_.empty()) {
        query_.pop_back();
    }
}

void EditorHierarchySearchState::SelectAll() noexcept {
    if (focused_) {
        selectingAll_ = true;
    }
}

void EditorHierarchySearchState::Clear() {
    query_.clear();
    selectingAll_ = false;
}

} // namespace kb::editor
