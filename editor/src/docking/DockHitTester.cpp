#include "docking/DockHitTester.hpp"

namespace kb::editor {

DockHit DockHitTester::HitTest(const DockLayout& layout, int x, int y) const {
    for (const DockSplitterLayout& splitter : layout.splitters) {
        if (splitter.rect.Contains(x, y)) {
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
