#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <string>
#include <string_view>

namespace kb::editor {

#if defined(_WIN32)

// Reads and writes UTF-8 text on the Windows clipboard.
class ScriptEditorClipboard {
public:
    ScriptEditorClipboard() = delete;

    [[nodiscard]] static std::string Get(HWND owner);
    static void Set(HWND owner, std::string_view text);
};

#endif

} // namespace kb::editor
