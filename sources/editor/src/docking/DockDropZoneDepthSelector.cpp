#include "docking/DockDropZoneDepths.hpp"

namespace kb::editor {

DockDropZone DockDropZoneDepthSelector::Select(const DockDropZoneDepths& depths) noexcept {
    if (!depths.left && !depths.right && !depths.top && !depths.bottom) {
        return DockDropZone::None;
    }

    int bestDepth = depths.leftDepth;
    DockDropZone bestZone = DockDropZone::Left;
    if (depths.rightDepth > bestDepth) {
        bestDepth = depths.rightDepth;
        bestZone = DockDropZone::Right;
    }
    if (depths.topDepth > bestDepth) {
        bestDepth = depths.topDepth;
        bestZone = DockDropZone::Top;
    }
    if (depths.bottomDepth > bestDepth) {
        bestZone = DockDropZone::Bottom;
    }
    return bestZone;
}

} // namespace kb::editor
