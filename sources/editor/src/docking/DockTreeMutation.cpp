#include "docking/DockTreeMutation.hpp"

#include "docking/DockCenterPanelInserter.hpp"
#include "docking/DockNodeFactory.hpp"
#include "docking/DockNodeIdSource.hpp"
#include "docking/DockPanelRemover.hpp"
#include "docking/DockSplitPanelInserter.hpp"
#include "docking/DockTreePruner.hpp"

namespace kb::editor {

void DockTreeMutation::RemovePanel(std::unique_ptr<DockNode>& root, std::uint32_t panelId) noexcept {
    DockPanelRemover::RemovePanel(root, panelId);
}

void DockTreeMutation::PruneEmptyBranches(std::unique_ptr<DockNode>& root) noexcept {
    DockTreePruner::PruneEmptyBranches(root);
}

void DockTreeMutation::DockPanel(std::unique_ptr<DockNode>& root, std::uint32_t panelId, const DockDropPreview& target, NextNodeIdFn nextNodeId, void* context) {
    DockNodeIdSource nodeIds{ nextNodeId, context };

    if (root == nullptr) {
        root = DockNodeFactory::MakeLeaf(nodeIds.Next(), { panelId });
        return;
    }

    if (target.zone == DockDropZone::Center) {
        DockCenterPanelInserter::Dock(root, panelId, target, nodeIds);
        return;
    }

    DockSplitPanelInserter::Dock(root, panelId, target, nodeIds);
}

} // namespace kb::editor
