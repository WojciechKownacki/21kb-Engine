#include "docking/DockDropPlacementPolicy.hpp"

namespace kb::editor {
namespace {

constexpr float kRootSplitRatio = 0.25F;
constexpr float kLeafSplitRatio = 0.50F;

} // namespace

DockSplitAxis DockDropPlacementPolicy::AxisForZone(DockDropZone zone) noexcept {
    return (zone == DockDropZone::Left || zone == DockDropZone::Right) ? DockSplitAxis::Horizontal : DockSplitAxis::Vertical;
}

bool DockDropPlacementPolicy::IsDroppedFirst(DockDropZone zone) noexcept {
    return zone == DockDropZone::Left || zone == DockDropZone::Top;
}

float DockDropPlacementPolicy::RatioForTarget(const DockDropPreview& target) noexcept {
    const bool droppedFirst = IsDroppedFirst(target.zone);
    return target.leafId == 0 ? (droppedFirst ? kRootSplitRatio : 1.0F - kRootSplitRatio) : kLeafSplitRatio;
}

} // namespace kb::editor
