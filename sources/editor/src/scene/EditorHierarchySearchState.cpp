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
}

void EditorHierarchySearchState::SetQuery(std::string query) {
    query_ = std::move(query);
}

void EditorHierarchySearchState::AppendAscii(wchar_t character) {
    if (character >= 32 && character < 127) {
        query_.push_back(static_cast<char>(character));
    }
}

void EditorHierarchySearchState::Backspace() {
    if (!query_.empty()) {
        query_.pop_back();
    }
}

void EditorHierarchySearchState::Clear() {
    query_.clear();
}

} // namespace kb::editor
