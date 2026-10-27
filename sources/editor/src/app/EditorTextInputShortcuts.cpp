#include "app/EditorTextInputShortcuts.hpp"
#include "app/EditorKeyState.hpp"
#include "engine/platform/win32/Win32ClipboardText.hpp"

#if defined(_WIN32)


namespace kb::editor {
namespace {

[[nodiscard]] bool ControlDown() noexcept {
    return EditorKeyDown(VK_CONTROL);
}

[[nodiscard]] bool AltDown() noexcept {
    return EditorKeyDown(VK_MENU);
}

} // namespace

EditorTextInputShortcut EditorTextInputShortcuts::Resolve(WPARAM key) noexcept {
    if (!ControlDown() || AltDown()) {
        return EditorTextInputShortcut::None;
    }

    switch (key) {
    case 'A':
        return EditorTextInputShortcut::SelectAll;
    case 'C':
        return EditorTextInputShortcut::Copy;
    case 'X':
        return EditorTextInputShortcut::Cut;
    case 'V':
        return EditorTextInputShortcut::Paste;
    default:
        return EditorTextInputShortcut::None;
    }
}

bool EditorTextInputShortcuts::CopyToClipboard(HWND owner, std::string_view text) noexcept {
    return kb::platform::WriteWin32ClipboardText(owner, text);
}

std::optional<std::string> EditorTextInputShortcuts::PasteFromClipboard(HWND owner, std::size_t maximumLength) {
    auto text = kb::platform::ReadWin32ClipboardText(owner, maximumLength);
    if (text) std::erase_if(*text, [](unsigned char value) { return value < 32U || value == 127U; });
    return text;
}

} // namespace kb::editor

#endif
