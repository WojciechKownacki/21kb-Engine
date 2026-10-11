#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstring>
#include <string>

namespace kb::editor {

// The editor's text is UTF-8 in every language it speaks. GDI's narrow calls (DrawTextA,
// GetTextExtentPoint32A, AppendMenuA, MessageBoxA) convert by the font's or the system's code
// page, not the process's UTF-8 one, so Japanese or Greek turned into mojibake. These take the
// narrow calls' parameters, read them as UTF-8 and call the wide functions.

[[nodiscard]] inline std::wstring WidenUtf8(const char* text, int length = -1) {
    if (text == nullptr) {
        return {};
    }
    const int bytes = length < 0 ? static_cast<int>(std::strlen(text)) : length;
    if (bytes == 0) {
        return {};
    }
    const int wideLength = MultiByteToWideChar(CP_UTF8, 0, text, bytes, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(wideLength > 0 ? wideLength : 0), L'\0');
    if (wideLength > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text, bytes, wide.data(), wideLength);
    }
    return wide;
}

inline int DrawTextUtf8(HDC dc, const char* text, int length, RECT* rect, UINT format) {
    const std::wstring wide = WidenUtf8(text, length);
    return DrawTextW(dc, wide.c_str(), static_cast<int>(wide.size()), rect, format);
}

inline BOOL GetTextExtentPoint32Utf8(HDC dc, const char* text, int length, SIZE* size) {
    const std::wstring wide = WidenUtf8(text, length);
    return GetTextExtentPoint32W(dc, wide.c_str(), static_cast<int>(wide.size()), size);
}

inline BOOL AppendMenuUtf8(HMENU menu, UINT flags, UINT_PTR id, const char* text) {
    // MF_SEPARATOR and friends pass no text.
    if (text == nullptr || (flags & (MF_BITMAP | MF_OWNERDRAW | MF_SEPARATOR)) != 0U) {
        return AppendMenuW(menu, flags, id, nullptr);
    }
    const std::wstring wide = WidenUtf8(text);
    return AppendMenuW(menu, flags, id, wide.c_str());
}

inline int MessageBoxUtf8(HWND owner, const char* text, const char* caption, UINT type) {
    const std::wstring wideText = WidenUtf8(text);
    const std::wstring wideCaption = WidenUtf8(caption);
    return MessageBoxW(owner, wideText.c_str(), wideCaption.c_str(), type);
}

} // namespace kb::editor
#endif
