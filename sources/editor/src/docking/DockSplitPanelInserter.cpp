#include "docking/DockSplitPanelInserter.hpp"

#include "docking/DockDropPlacementPolicy.hpp"
#include "docking/DockNodeFactory.hpp"
#include "docking/DockNodeIdSource.hpp"
#include "docking/DockNodeQuery.hpp"

#include <utility>

namespace kb::editor {

void DockSplitPanelInserter::Dock(std::unique_ptr<DockNode>& root, std::uint32_t panelId, const DockDropPreview& target, DockNodeIdSource& nodeIds) {
    const DockSplitAxis axis = DockDropPlacementPolicy::AxisForZone(target.zone);
    const bool droppedFirst = DockDropPlacementPolicy::IsDroppedFirst(target.zone);
    const float ratio = DockDropPlacementPolicy::RatioForTarget(target);
    auto dropped = DockNodeFactory::MakeLeaf(nodeIds.Next(), { panelId });

    if (target.leafId == 0) {
        AttachToRoot(root, std::move(dropped), axis, ratio, droppedFirst, nodeIds);
        return;
    }

    std::unique_ptr<DockNode>* slot = DockNodeQuery::FindNodeSlot(root, target.leafId);
    if (slot == nullptr || *slot == nullptr) {
        AttachToRoot(root, std::move(dropped), axis, ratio, droppedFirst, nodeIds);
        return;
    }

    auto oldLeaf = std::move(*slot);
    *slot = droppedFirst
                ? DockNodeFactory::MakeSplit(nodeIds.Next(), axis, ratio, std::move(dropped), std::move(oldLeaf))
                : DockNodeFactory::MakeSplit(nodeIds.Next(), axis, ratio, std::move(oldLeaf), std::move(dropped));
}

void DockSplitPanelInserter::AttachToRoot(
    std::unique_ptr<DockNode>& root,
    std::unique_ptr<DockNode> dropped,
    DockSplitAxis axis,
    float ratio,
    bool droppedFirst,
    DockNodeIdSource& nodeIds) {
    root = droppedFirst
                ? DockNodeFactory::MakeSplit(nodeIds.Next(), axis, ratio, std::move(dropped), std::move(root))
                : DockNodeFactory::MakeSplit(nodeIds.Next(), axis, ratio, std::move(root), std::move(dropped));
}

} // namespace kb::editor
