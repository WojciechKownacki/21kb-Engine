#pragma once

#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace kb::editor {

// A Windows file dialog filter from UTF-8 entries: each is the description the dialog shows (in the
// editor language) and its pattern, "*.png;*.jpg". Entries are '\0'-separated and the list ends with
// an empty one, as OPENFILENAMEW::lpstrFilter expects.
[[nodiscard]] inline std::wstring EditorFileDialogFilter(
    std::initializer_list<std::pair<std::string_view, std::string_view>> entries) {
    std::wstring filter;
    for (const auto& [description, pattern] : entries) {
        filter += ScriptEditorTextEncoding::Widen(description);
        filter.push_back(L'\0');
        filter += ScriptEditorTextEncoding::Widen(pattern);
        filter.push_back(L'\0');
    }
    filter.push_back(L'\0');
    return filter;
}

} // namespace kb::editor
