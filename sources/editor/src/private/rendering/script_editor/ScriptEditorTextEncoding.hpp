#pragma once

#include <string>
#include <string_view>

namespace kb::editor {

// UTF-8 <-> UTF-16 conversion used by the Win32 text control and clipboard.
// Isolated so encoding lives in exactly one place.
class ScriptEditorTextEncoding {
public:
    ScriptEditorTextEncoding() = delete;

    [[nodiscard]] static std::wstring Widen(std::string_view utf8);
    [[nodiscard]] static std::string Narrow(std::wstring_view utf16);
};

} // namespace kb::editor
