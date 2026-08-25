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

struct DenseListRowLayout {
    RECT bounds{};
    RECT icon{};
    RECT text{};
};

struct DenseListRowDescriptor {
    RECT bounds{};
    std::string_view title{};
    std::string_view summary{};
    HeroIconKind icon = HeroIconKind::Cube;
    int contentLeftInset = 8;
    int contentRightInset = 8;
    bool selected = false;
    bool hovered = false;
    bool enabled = true;
    bool showIcon = false;
    bool showDivider = true;
};

class DenseListRow final {
public:
    DenseListRow() = delete;

    static constexpr int PreferredHeight = 24;
    static constexpr int IconSize = 16;
    static constexpr int IconGap = 6;

    [[nodiscard]] static DenseListRowLayout Resolve(
        const RECT& bounds,
        int contentLeftInset = 8,
        int contentRightInset = 8,
        bool showIcon = false) noexcept {
        const int height = std::max(0, static_cast<int>(bounds.bottom - bounds.top));
        const int contentLeft = std::clamp<LONG>(
            bounds.left + std::max(0, contentLeftInset), bounds.left, bounds.right);
        const int contentRight = std::clamp<LONG>(
            bounds.right - std::max(0, contentRightInset), contentLeft, bounds.right);
        const int iconTop = bounds.top + (height - IconSize) / 2;
        const RECT icon = showIcon
            ? RECT{contentLeft, std::max<LONG>(bounds.top, iconTop),
                   std::min<LONG>(contentRight, contentLeft + IconSize),
                   std::min<LONG>(bounds.bottom, iconTop + IconSize)}
            : RECT{};
        return DenseListRowLayout{
            .bounds = bounds,
            .icon = icon,
            .text = RECT{showIcon ? std::min<LONG>(contentRight, icon.right + IconGap) : contentLeft,
                         bounds.top, contentRight, bounds.bottom},
        };
    }

    static void Paint(HDC dc, const EditorTheme& theme, const DenseListRowDescriptor& descriptor);
};

#endif

} // namespace kb::editor
