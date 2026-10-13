#pragma once

#if defined(_WIN32)
#include "rendering/gdi/EditorFontCache.hpp"

#include <algorithm>
#include <initializer_list>
#include <string>
#include <string_view>

namespace kb::editor {

// Width in pixels of UTF-8 `text` in the editor's font (Segoe UI, `pointSize`, `weight`), measured
// the way GDI draws it. For layouts, which run without a DC: a button or a column sized for its
// label fits the label in every editor language.
[[nodiscard]] inline int MeasureTextWidth(std::string_view text, int pointSize, int weight = FW_NORMAL) noexcept {
    try {
        static HDC measuring = CreateCompatibleDC(nullptr);
        if (measuring == nullptr || text.empty()) {
            return 0;
        }
        const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (length <= 0) {
            return 0;
        }
        std::wstring wide(static_cast<std::size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
        const HGDIOBJ previous = SelectObject(measuring, EditorFontCache::Acquire(pointSize, weight));
        SIZE size{};
        const BOOL measured = GetTextExtentPoint32W(measuring, wide.data(), length, &size);
        SelectObject(measuring, previous);
        return measured != 0 ? static_cast<int>(size.cx) : 0;
    } catch (...) {
        return 0;
    }
}

// The width a control needs for the widest of `labels` plus `padding`, never below `minimum`.
[[nodiscard]] inline int FitTextWidth(int minimum, std::initializer_list<std::string_view> labels, int pointSize,
    int weight, int padding) noexcept {
    int width = minimum;
    for (const std::string_view label : labels) {
        width = std::max(width, MeasureTextWidth(label, pointSize, weight) + padding);
    }
    return width;
}

} // namespace kb::editor
#endif
