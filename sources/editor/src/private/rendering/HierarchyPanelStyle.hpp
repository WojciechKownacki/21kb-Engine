#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorHierarchyMetrics.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class HierarchyPanelStyle {
public:
    HierarchyPanelStyle() = delete;

    static constexpr int HeaderHeight = kHierarchyHeaderHeight;
    static constexpr int RowHeight = kHierarchyRowHeight;
    static constexpr int SearchHeight = 22;
    static constexpr int ButtonWidth = 24;
    static constexpr int DropdownWidth = 14;
    static constexpr int OptionsWidth = 20;
    static constexpr int HeaderPadLeft = 6;
    static constexpr int HeaderPadRight = 8;
    static constexpr int HeaderPadY = 6;
    static constexpr int HeaderGap = 6;
    static constexpr int EyeColumnWidth = 24;
    static constexpr int IndentPerDepth = 14;
    static constexpr int RowIconSize = 16;
    static constexpr int RowIconGap = 6;
    static constexpr int TextTopPadding = 3;

#if defined(_WIN32)
    [[nodiscard]] static COLORREF PanelBackground() noexcept;
    [[nodiscard]] static COLORREF HeaderBackground() noexcept;
    [[nodiscard]] static COLORREF HeaderBorder() noexcept;
    [[nodiscard]] static COLORREF SearchBackground() noexcept;
    [[nodiscard]] static COLORREF SearchBorder() noexcept;
    [[nodiscard]] static COLORREF SearchFocusedBorder() noexcept;
    [[nodiscard]] static COLORREF SearchPlaceholderText() noexcept;
    [[nodiscard]] static COLORREF RowSelected() noexcept;
    [[nodiscard]] static COLORREF RowHover() noexcept;
    [[nodiscard]] static COLORREF RowText() noexcept;
    [[nodiscard]] static COLORREF RowTextSelected() noexcept;
    [[nodiscard]] static COLORREF RowTextHidden() noexcept;
    [[nodiscard]] static COLORREF CubeStroke() noexcept;
    [[nodiscard]] static COLORREF PrefabCubeStroke() noexcept;
    [[nodiscard]] static COLORREF CubeFill() noexcept;
    [[nodiscard]] static COLORREF MutedText() noexcept;
#endif
};

} // namespace kb::editor
