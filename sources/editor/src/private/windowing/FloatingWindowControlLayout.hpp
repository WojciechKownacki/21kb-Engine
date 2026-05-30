#pragma once

#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "windowing/FloatingWindowControlKind.hpp"

#include <array>

namespace kb::editor {

class FloatingWindowControlLayout {
public:
    FloatingWindowControlLayout() = delete;

    static constexpr int ControlCount = 3;
    static constexpr int TopInset = 1;
    static constexpr std::array<FloatingWindowControlKind, ControlCount> OrderedControls{
        FloatingWindowControlKind::Minimize,
        FloatingWindowControlKind::MaximizeRestore,
        FloatingWindowControlKind::Close,
    };

    [[nodiscard]] static int TotalWidth(const EditorMetrics& metrics) noexcept {
        return metrics.floatingControlWidth * ControlCount;
    }

    [[nodiscard]] static DockRect Rect(const EditorMetrics& metrics, int clientWidth, FloatingWindowControlKind control) noexcept {
        const int top = TopInset;
        const int height = metrics.tabStripHeight - TopInset;
        const int closeX = clientWidth - metrics.floatingControlWidth;
        switch (control) {
        case FloatingWindowControlKind::Minimize:
            return DockRect{ closeX - (metrics.floatingControlWidth * 2), top, metrics.floatingControlWidth, height };
        case FloatingWindowControlKind::MaximizeRestore:
            return DockRect{ closeX - metrics.floatingControlWidth, top, metrics.floatingControlWidth, height };
        case FloatingWindowControlKind::Close:
            return DockRect{ closeX, top, metrics.floatingControlWidth, height };
        case FloatingWindowControlKind::None:
        default:
            return {};
        }
    }

    [[nodiscard]] static DockRect StripDragRect(const EditorMetrics& metrics, int clientWidth) noexcept {
        const int width = clientWidth > TotalWidth(metrics) ? clientWidth - TotalWidth(metrics) : 0;
        return DockRect{ 0, 0, width, metrics.tabStripHeight };
    }
};

} // namespace kb::editor
