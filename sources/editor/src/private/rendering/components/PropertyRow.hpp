#pragma once

#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <algorithm>
#include <string_view>

namespace kb::editor {

#if defined(_WIN32)

enum class PropertyRowValueAlignment {
    Left,
    Center,
    Right,
};

struct PropertyRowLayout {
    RECT bounds{};
    RECT label{};
    RECT value{};
};

struct PropertyRowDescriptor {
    RECT bounds{};
    std::string_view label{};
    std::string_view value{};
    bool hovered = false;
    bool valueHovered = false;
    bool enabled = true;
    bool editable = true;
    PropertyRowValueAlignment valueAlignment = PropertyRowValueAlignment::Left;
};

class PropertyRow final {
public:
    PropertyRow() = delete;

    static constexpr int PreferredHeight = 24;
    static constexpr int ValueHeight = 20;
    static constexpr int HorizontalPadding = 16;
    static constexpr int LabelWidthPercent = 36;

    [[nodiscard]] static PropertyRowLayout Resolve(
        const RECT& bounds,
        int horizontalPadding = HorizontalPadding,
        int labelWidthPercent = LabelWidthPercent,
        int valueHeight = ValueHeight) noexcept {
        const int width = std::max(0, static_cast<int>(bounds.right - bounds.left));
        const int height = std::max(0, static_cast<int>(bounds.bottom - bounds.top));
        const int padding = std::clamp(horizontalPadding, 0, width / 2);
        const int clampedValueHeight = std::clamp(valueHeight, 0, std::max(0, height));
        const int valueTop = bounds.top + (height - clampedValueHeight) / 2;
        const int contentLeft = bounds.left + padding;
        const int contentRight = bounds.right - padding;
        const int split = std::clamp<int>(
            static_cast<int>(bounds.left) + width * std::clamp(labelWidthPercent, 0, 100) / 100,
            contentLeft,
            contentRight);
        return PropertyRowLayout{
            .bounds = bounds,
            .label = RECT{contentLeft, bounds.top, split, bounds.bottom},
            .value = RECT{split, valueTop, contentRight, valueTop + clampedValueHeight},
        };
    }

    static void PaintBackground(HDC dc, const EditorTheme& theme, const RECT& bounds, bool hovered);
    static void PaintLabel(
        HDC dc,
        const EditorTheme& theme,
        RECT bounds,
        std::string_view label,
        bool enabled = true,
        bool topAligned = false);
    static void PaintValue(
        HDC dc,
        const EditorTheme& theme,
        RECT bounds,
        std::string_view value,
        bool hovered = false,
        bool enabled = true,
        PropertyRowValueAlignment alignment = PropertyRowValueAlignment::Left);
    static void Paint(HDC dc, const EditorTheme& theme, const PropertyRowDescriptor& descriptor);
};

#endif

} // namespace kb::editor
