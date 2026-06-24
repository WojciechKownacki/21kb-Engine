#pragma once

#include "kb/editor/docking/DockTypes.hpp"

#include <algorithm>

namespace kb::editor {

class DockTabControlGeometry {
public:
    DockTabControlGeometry() = delete;

    static constexpr int CloseSize = 16;
    static constexpr int CloseRightInset = 7;
    static constexpr int TextRightPadding = CloseSize + CloseRightInset + 7;

    [[nodiscard]] static DockRect CloseRect(const DockRect& tab) noexcept {
        const int size = std::min(CloseSize, std::max(0, tab.height - 8));
        if (size <= 0 || tab.width < size + CloseRightInset) {
            return {};
        }
        return DockRect{
            .x = tab.x + tab.width - CloseRightInset - size,
            .y = tab.y + ((tab.height - size) / 2),
            .width = size,
            .height = size,
        };
    }

    [[nodiscard]] static bool ContainsClose(const DockRect& tab, int x, int y) noexcept {
        return CloseRect(tab).Contains(x, y);
    }
};

} // namespace kb::editor
