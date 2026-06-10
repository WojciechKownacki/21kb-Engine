#include "rendering/script_editor/ScriptEditorClipboard.hpp"

#if defined(_WIN32)
#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"

#include <cstring>

namespace kb::editor {

std::string ScriptEditorClipboard::Get(HWND owner) {
    if (OpenClipboard(owner) == 0) {
        return {};
    }
    std::string text;
    if (HANDLE handle = GetClipboardData(CF_UNICODETEXT); handle != nullptr) {
        if (const auto* memory = static_cast<const wchar_t*>(GlobalLock(handle)); memory != nullptr) {
            text = ScriptEditorTextEncoding::Narrow(memory);
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    return text;
}

void ScriptEditorClipboard::Set(HWND owner, std::string_view text) {
    if (OpenClipboard(owner) == 0) {
        return;
    }
    EmptyClipboard();
    const std::wstring wide = ScriptEditorTextEncoding::Widen(text);
    const std::size_t bytes = (wide.size() + 1U) * sizeof(wchar_t);
    if (HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes); handle != nullptr) {
        if (void* memory = GlobalLock(handle); memory != nullptr) {
            std::memcpy(memory, wide.c_str(), bytes);
            GlobalUnlock(handle);
            SetClipboardData(CF_UNICODETEXT, handle);
        }
    }
    CloseClipboard();
}

} // namespace kb::editor

#endif
