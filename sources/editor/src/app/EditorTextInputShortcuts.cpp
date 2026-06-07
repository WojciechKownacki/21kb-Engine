#include "app/EditorTextInputShortcuts.hpp"

#if defined(_WIN32)

#include <cstring>

namespace kb::editor {
namespace {

[[nodiscard]] bool ControlDown() noexcept {
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
}

[[nodiscard]] bool AltDown() noexcept {
    return (GetKeyState(VK_MENU) & 0x8000) != 0;
}

[[nodiscard]] std::wstring ToWide(std::string_view text) {
    std::wstring wide;
    wide.reserve(text.size());
    for (const char character : text) {
        wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(character)));
    }
    return wide;
}

[[nodiscard]] std::string ToEditorText(const wchar_t* text) {
    std::string result;
    if (text == nullptr) {
        return result;
    }
    for (const wchar_t* cursor = text; *cursor != L'\0'; ++cursor) {
        if (*cursor >= 32 && *cursor < 127) {
            result.push_back(static_cast<char>(*cursor));
        }
    }
    return result;
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
    const std::wstring wide = ToWide(text);
    const SIZE_T byteCount = (wide.size() + 1U) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, byteCount);
    if (memory == nullptr) {
        return false;
    }

    void* locked = GlobalLock(memory);
    if (locked == nullptr) {
        GlobalFree(memory);
        return false;
    }
    std::memcpy(locked, wide.c_str(), byteCount);
    GlobalUnlock(memory);

    if (!OpenClipboard(owner)) {
        GlobalFree(memory);
        return false;
    }
    EmptyClipboard();
    const HANDLE handle = SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
    if (handle == nullptr) {
        GlobalFree(memory);
        return false;
    }
    return true;
}

std::optional<std::string> EditorTextInputShortcuts::PasteFromClipboard(HWND owner) {
    if (!OpenClipboard(owner)) {
        return std::nullopt;
    }

    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle == nullptr) {
        CloseClipboard();
        return std::nullopt;
    }

    const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(handle));
    if (text == nullptr) {
        CloseClipboard();
        return std::nullopt;
    }

    std::string result = ToEditorText(text);
    GlobalUnlock(handle);
    CloseClipboard();
    return result;
}

} // namespace kb::editor

#endif
