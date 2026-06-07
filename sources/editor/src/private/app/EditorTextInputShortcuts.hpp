#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <optional>
#include <string>
#include <string_view>

namespace kb::editor {

enum class EditorTextInputShortcut {
    None,
    SelectAll,
    Copy,
    Cut,
    Paste,
};

class EditorTextInputShortcuts {
public:
    EditorTextInputShortcuts() = delete;

#if defined(_WIN32)
    [[nodiscard]] static EditorTextInputShortcut Resolve(WPARAM key) noexcept;
    [[nodiscard]] static bool CopyToClipboard(HWND owner, std::string_view text) noexcept;
    [[nodiscard]] static std::optional<std::string> PasteFromClipboard(HWND owner);
#endif
};

} // namespace kb::editor
