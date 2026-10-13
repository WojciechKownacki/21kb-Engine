#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "rendering/gdi/CutTextAudit.hpp"

#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

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

// DrawTextW, and the cut-text record when it is on.
inline int DrawTextWide(HDC dc, const wchar_t* text, int length, RECT* rect, UINT format) {
    // An ellipsis loses the end of the text as well, so it counts as cut too.
    if (std::vector<CutText>* sink = CutTextSink();
        sink != nullptr && rect != nullptr && text != nullptr && (format & DT_SINGLELINE) != 0U &&
        (format & DT_CALCRECT) == 0U) {
        const int count = length < 0 ? static_cast<int>(std::wcslen(text)) : length;
        SIZE size{};
        // The text's own rectangle, or less where the panel's clip region ends before it.
        RECT clip{};
        LONG left = rect->left;
        LONG right = rect->right;
        if (GetClipBox(dc, &clip) != ERROR && clip.right > clip.left) {
            left = left > clip.left ? left : clip.left;
            right = right < clip.right ? right : clip.right;
        }
        const int available = static_cast<int>(right > left ? right - left : 0);
        if (count > 0 && GetTextExtentPoint32W(dc, text, count, &size) != 0 && size.cx > available + 1) {
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, count, nullptr, 0, nullptr, nullptr);
            std::string utf8(static_cast<std::size_t>(bytes > 0 ? bytes : 0), '\0');
            if (bytes > 0) {
                WideCharToMultiByte(CP_UTF8, 0, text, count, utf8.data(), bytes, nullptr, nullptr);
            }
            sink->push_back(CutText{ std::move(utf8), static_cast<int>(size.cx), available });
        }
    }
    return DrawTextW(dc, text, length, rect, format);
}

inline int DrawTextUtf8(HDC dc, const char* text, int length, RECT* rect, UINT format) {
    const std::wstring wide = WidenUtf8(text, length);
    return DrawTextWide(dc, wide.c_str(), static_cast<int>(wide.size()), rect, format);
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
