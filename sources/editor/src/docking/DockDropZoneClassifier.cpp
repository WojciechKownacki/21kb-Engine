#include "docking/DockDropZoneClassifier.hpp"

#include "docking/DockDropEdgeBandClassifier.hpp"
#include "docking/DockDropPreviewMetrics.hpp"

#include <algorithm>
#include <cmath>

namespace kb::editor {

DockDropZone DockDropZoneClassifier::DominantOuterEdge(const DockRect& rect, int x, int y) const noexcept {
    return DockDropEdgeBandClassifier::Classify(rect, x, y, dock_preview_metrics::OuterEdgeBand);
}

DockDropZone DockDropZoneClassifier::ClassifyLeafZone(const DockRect& rect, int x, int y) const noexcept {
    const int band = static_cast<int>(std::round(static_cast<float>(std::min(rect.width, rect.height)) * dock_preview_metrics::LeafEdgeBand));
    return DockDropEdgeBandClassifier::Classify(rect, x, y, band);
}

} // namespace kb::editor
