#include "docking/DockDropZoneClassifier.hpp"

#include "docking/DockDropPreviewMetrics.hpp"

#include <algorithm>
#include <cmath>

namespace kb::editor {

DockDropZone DockDropZoneClassifier::DominantOuterEdge(const DockRect& rect, int x, int y) const noexcept {
    const bool nearLeft = x < rect.x + dock_preview_metrics::OuterEdgeBand;
    const bool nearRight = x > rect.x + rect.width - dock_preview_metrics::OuterEdgeBand;
    const bool nearTop = y < rect.y + dock_preview_metrics::OuterEdgeBand;
    const bool nearBottom = y > rect.y + rect.height - dock_preview_metrics::OuterEdgeBand;

    return DominantZone(
        nearLeft,
        nearRight,
        nearTop,
        nearBottom,
        nearLeft ? rect.x + dock_preview_metrics::OuterEdgeBand - x : -1,
        nearRight ? x - (rect.x + rect.width - dock_preview_metrics::OuterEdgeBand) : -1,
        nearTop ? rect.y + dock_preview_metrics::OuterEdgeBand - y : -1,
        nearBottom ? y - (rect.y + rect.height - dock_preview_metrics::OuterEdgeBand) : -1);
}

DockDropZone DockDropZoneClassifier::ClassifyLeafZone(const DockRect& rect, int x, int y) const noexcept {
    const int band = static_cast<int>(std::round(static_cast<float>(std::min(rect.width, rect.height)) * dock_preview_metrics::LeafEdgeBand));
    const bool inLeft = x < rect.x + band;
    const bool inRight = x > rect.x + rect.width - band;
    const bool inTop = y < rect.y + band;
    const bool inBottom = y > rect.y + rect.height - band;

    return DominantZone(
        inLeft,
        inRight,
        inTop,
        inBottom,
        inLeft ? rect.x + band - x : -1,
        inRight ? x - (rect.x + rect.width - band) : -1,
        inTop ? rect.y + band - y : -1,
        inBottom ? y - (rect.y + rect.height - band) : -1);
}

DockDropZone DockDropZoneClassifier::DominantZone(bool left, bool right, bool top, bool bottom, int leftDepth, int rightDepth, int topDepth, int bottomDepth) noexcept {
    if (!left && !right && !top && !bottom) {
        return DockDropZone::None;
    }

    int bestDepth = leftDepth;
    DockDropZone bestZone = DockDropZone::Left;
    if (rightDepth > bestDepth) {
        bestDepth = rightDepth;
        bestZone = DockDropZone::Right;
    }
    if (topDepth > bestDepth) {
        bestDepth = topDepth;
        bestZone = DockDropZone::Top;
    }
    if (bottomDepth > bestDepth) {
        bestZone = DockDropZone::Bottom;
    }
    return bestZone;
}

} // namespace kb::editor
