#include "docking/DockHitTester.hpp"

namespace kb::editor {

DockHit DockHitTester::HitTest(const DockLayout& layout, int x, int y) const {
    // The seam between panels is thin to look at; the pointer gets a wider target than the eye.
    constexpr int kSplitterGrabMargin = 2;
    for (const DockSplitterLayout& splitter : layout.splitters) {
        const DockRect grab{
            .x = splitter.rect.x - kSplitterGrabMargin,
            .y = splitter.rect.y - kSplitterGrabMargin,
            .width = splitter.rect.width + (kSplitterGrabMargin * 2),
            .height = splitter.rect.height + (kSplitterGrabMargin * 2),
        };
        if (grab.Contains(x, y)) {
            return DockHit{ .kind = DockHitKind::Splitter, .splitterNodeId = splitter.nodeId };
        }
    }
    for (const DockPanelLayout& panel : layout.panels) {
        if (panel.tab.Contains(x, y)) {
            return DockHit{ .kind = DockHitKind::Tab, .panelId = panel.panelId, .leafId = panel.leafId };
        }
    }
    return {};
}

} // namespace kb::editor
