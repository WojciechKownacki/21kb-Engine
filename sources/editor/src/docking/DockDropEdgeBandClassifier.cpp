#include "docking/DockDropEdgeBandClassifier.hpp"

namespace kb::editor {

DockDropZone DockDropEdgeBandClassifier::Classify(const DockRect& rect, int x, int y, int band) noexcept {
    const bool inLeft = x < rect.x + band;
    const bool inRight = x > rect.x + rect.width - band;
    const bool inTop = y < rect.y + band;
    const bool inBottom = y > rect.y + rect.height - band;

    return DockDropZoneDepthSelector::Select(DockDropZoneDepths{
        .left = inLeft,
        .right = inRight,
        .top = inTop,
        .bottom = inBottom,
        .leftDepth = inLeft ? rect.x + band - x : -1,
        .rightDepth = inRight ? x - (rect.x + rect.width - band) : -1,
        .topDepth = inTop ? rect.y + band - y : -1,
        .bottomDepth = inBottom ? y - (rect.y + rect.height - band) : -1,
    });
}

} // namespace kb::editor
