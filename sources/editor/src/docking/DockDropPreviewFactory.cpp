#include "docking/DockDropPreviewFactory.hpp"

#include "docking/DockDropPreviewMetrics.hpp"
#include "docking/DockGeometry.hpp"

#include <algorithm>

namespace kb::editor {

DockDropPreview DockDropPreviewFactory::ForStripMarker(
    const DockLayout& layout,
    const DockLeafLayout& leaf,
    int x) const noexcept {
    const std::uint32_t insertionIndex = StripInsertionIndex(layout, leaf.leafId, x);
    return DockDropPreview{
        .zone = DockDropZone::Center,
        .kind = DockDropPreviewKind::StripMarker,
        .leafId = leaf.leafId,
        .rect = DockGeometry::MakeRect(
            StripMarkerX(layout, leaf, insertionIndex),
            leaf.tabStrip.y,
            dock_preview_metrics::StripMarkerWidth,
            leaf.tabStrip.height),
        .tabInsertionIndex = insertionIndex,
    };
}

DockDropPreview DockDropPreviewFactory::ForRootEdge(const DockRect& workspace, DockDropZone zone) const noexcept {
    return DockDropPreview{
        .zone = zone,
        .kind = DockDropPreviewKind::Glow,
        .rect = DockGeometry::Split(workspace, zone, dock_preview_metrics::RootSplitRatio),
    };
}

DockDropPreview DockDropPreviewFactory::ForLeafEdge(const DockLeafLayout& leaf, DockDropZone zone) const noexcept {
    return DockDropPreview{
        .zone = zone,
        .kind = DockDropPreviewKind::Glow,
        .leafId = leaf.leafId,
        .rect = DockGeometry::Split(leaf.content, zone, dock_preview_metrics::LeafSplitRatio),
    };
}

DockDropPreview DockDropPreviewFactory::ForEmptyWorkspace(const DockRect& workspace) const noexcept {
    return DockDropPreview{
        .zone = DockDropZone::Center,
        .kind = DockDropPreviewKind::Glow,
        .rect = workspace,
    };
}

std::uint32_t DockDropPreviewFactory::StripInsertionIndex(const DockLayout& layout, std::uint32_t leafId, int x) noexcept {
    std::uint32_t index = 0;
    for (const DockPanelLayout& panel : layout.panels) {
        if (panel.leafId != leafId) {
            continue;
        }

        const int midpoint = panel.tab.x + (panel.tab.width / 2);
        if (x < midpoint) {
            return index;
        }
        ++index;
    }
    return index;
}

int DockDropPreviewFactory::StripMarkerX(
    const DockLayout& layout,
    const DockLeafLayout& leaf,
    std::uint32_t insertionIndex) noexcept {
    int fallbackX = leaf.tabStrip.x;
    std::uint32_t index = 0;
    for (const DockPanelLayout& panel : layout.panels) {
        if (panel.leafId != leaf.leafId) {
            continue;
        }

        if (index == insertionIndex) {
            return std::clamp(
                panel.tab.x - (dock_preview_metrics::StripMarkerWidth / 2),
                leaf.tabStrip.x,
                leaf.tabStrip.x + leaf.tabStrip.width - dock_preview_metrics::StripMarkerWidth);
        }

        fallbackX = panel.tab.x + panel.tab.width;
        ++index;
    }

    return std::clamp(
        fallbackX - (dock_preview_metrics::StripMarkerWidth / 2),
        leaf.tabStrip.x,
        leaf.tabStrip.x + leaf.tabStrip.width - dock_preview_metrics::StripMarkerWidth);
}

} // namespace kb::editor
