#include "docking/DockPointerDragFactory.hpp"

#if defined(_WIN32)
#include "docking/EditorFloatingWindowManager.hpp"
#include "windowing/FloatingWindowControlLayout.hpp"

namespace kb::editor {

DockPointerDrag DockPointerDragFactory::FromDockHit(HWND window, const DockLayout& layout, const DockHit& hit, int x, int y) noexcept {
    return DockPointerDrag{
        .kind = hit.kind,
        .panelId = hit.panelId,
        .offsetX = 18,
        .offsetY = 14,
        .startX = x,
        .startY = y,
        .splitterNodeId = hit.splitterNodeId,
        .sourceLeafId = hit.leafId,
        .sourceTabIndex = SourceTabIndexForHit(layout, hit),
        .sourceStrip = SourceStripForHit(layout, hit),
        .sourceWindow = window,
    };
}

DockPointerDrag DockPointerDragFactory::FromFloatingWindow(HWND window, std::uint32_t panelId, int x, int y, const EditorMetrics& metrics) noexcept {
    POINT screen{ x, y };
    ClientToScreen(window, &screen);

    RECT frame{};
    GetWindowRect(window, &frame);

    return DockPointerDrag{
        .kind = DockHitKind::Tab,
        .panelId = panelId,
        .offsetX = screen.x - frame.left,
        .offsetY = screen.y - frame.top,
        .startX = x,
        .startY = y,
        .sourceLeafId = 0,
        .sourceTabIndex = 0,
        .sourceStrip = FloatingWindowControlLayout::StripDragRect(metrics, frame.right - frame.left),
        .sourceWindow = window,
        .detached = true,
    };
}

DockRect DockPointerDragFactory::SourceStripForHit(const DockLayout& layout, const DockHit& hit) noexcept {
    if (hit.kind != DockHitKind::Tab) {
        return {};
    }

    for (const DockLeafLayout& leaf : layout.leaves) {
        if (leaf.leafId == hit.leafId) {
            return leaf.tabStrip;
        }
    }
    return {};
}

std::uint32_t DockPointerDragFactory::SourceTabIndexForHit(const DockLayout& layout, const DockHit& hit) noexcept {
    if (hit.kind != DockHitKind::Tab) {
        return 0;
    }

    std::uint32_t leafIndex = 0;
    for (const DockPanelLayout& panel : layout.panels) {
        if (panel.leafId != hit.leafId) {
            continue;
        }
        if (panel.panelId == hit.panelId) {
            return leafIndex;
        }
        ++leafIndex;
    }
    return 0;
}

} // namespace kb::editor

#endif
