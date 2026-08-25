#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/HeroIconKind.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <algorithm>
#include <string_view>

namespace kb::editor {

#if defined(_WIN32)

struct CategoryHeaderLayout {
    RECT bounds{};
    RECT disclosure{};
    RECT icon{};
    RECT title{};
    RECT trailingText{};
    RECT trailingAction{};
};

struct CategoryHeaderDescriptor {
    RECT bounds{};
    std::string_view title{};
    std::string_view trailingText{};
    HeroIconKind icon = HeroIconKind::RectangleGroup;
    HeroIconKind trailingActionIcon = HeroIconKind::XMark;
    bool expanded = true;
    bool hovered = false;
    bool enabled = true;
    bool showIcon = true;
    bool showTrailingAction = false;
    bool trailingActionHovered = false;
};

class CategoryHeader final {
public:
    CategoryHeader() = delete;

    static constexpr int PreferredHeight = 24;
    static constexpr int TrailingActionSize = 18;

    [[nodiscard]] static CategoryHeaderLayout Resolve(
        const RECT& bounds,
        bool showIcon = true,
        bool showTrailingAction = false,
        bool showTrailingText = false) noexcept {
        const int height = std::max(0, static_cast<int>(bounds.bottom - bounds.top));
        const auto clipped = [&bounds](RECT rect) noexcept {
            rect.left = std::clamp(rect.left, bounds.left, bounds.right);
            rect.right = std::clamp(rect.right, rect.left, bounds.right);
            rect.top = std::clamp(rect.top, bounds.top, bounds.bottom);
            rect.bottom = std::clamp(rect.bottom, rect.top, bounds.bottom);
            return rect;
        };
        const int actionTop = bounds.top + (height - TrailingActionSize) / 2;
        const RECT action = showTrailingAction
            ? clipped(RECT{bounds.right - 34, actionTop, bounds.right - 16, actionTop + TrailingActionSize})
            : RECT{};
        const RECT trailing = showTrailingText
            ? clipped(RECT{bounds.right - (showTrailingAction ? 90 : 56), bounds.top,
                           bounds.right - (showTrailingAction ? 44 : 12), bounds.bottom})
            : RECT{};
        const int titleRight = showTrailingText
            ? trailing.left - 8
            : (showTrailingAction ? action.left - 10 : bounds.right - 8);
        const int titleLeft = std::clamp<LONG>(bounds.left + (showIcon ? 59 : 35), bounds.left, bounds.right);
        return CategoryHeaderLayout{
            .bounds = bounds,
            .disclosure = clipped(RECT{bounds.left + 9, bounds.top, bounds.left + 29, bounds.bottom}),
            .icon = showIcon
                ? clipped(RECT{bounds.left + 35, bounds.top + (height - 18) / 2,
                               bounds.left + 53, bounds.top + (height - 18) / 2 + 18})
                : RECT{},
            .title = RECT{titleLeft, bounds.top,
                          std::clamp<LONG>(titleRight, titleLeft, bounds.right), bounds.bottom},
            .trailingText = trailing,
            .trailingAction = action,
        };
    }

    static void Paint(HDC dc, const EditorTheme& theme, const CategoryHeaderDescriptor& descriptor);
};

#endif

} // namespace kb::editor
