#include "docking/DockDropPreviewFactory.hpp"

#include "docking/DockDropPreviewMetrics.hpp"
#include "docking/DockGeometry.hpp"

namespace kb::editor {

DockDropPreview DockDropPreviewFactory::ForStripMarker(const DockLeafLayout& leaf) const noexcept {
    return DockDropPreview{
        .zone = DockDropZone::Center,
        .kind = DockDropPreviewKind::StripMarker,
        .leafId = leaf.leafId,
        .rect = DockGeometry::MakeRect(leaf.tabStrip.x, leaf.tabStrip.y + leaf.tabStrip.height, leaf.tabStrip.width, dock_preview_metrics::StripMarkerHeight),
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

} // namespace kb::editor
