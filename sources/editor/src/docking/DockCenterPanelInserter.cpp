#include "docking/DockCenterPanelInserter.hpp"

#include "docking/DockNodeFactory.hpp"
#include "docking/DockNodeIdSource.hpp"
#include "docking/DockNodeQuery.hpp"

#include <algorithm>
#include <utility>

namespace kb::editor {

void DockCenterPanelInserter::Dock(std::unique_ptr<DockNode>& root, std::uint32_t panelId, const DockDropPreview& target, DockNodeIdSource& nodeIds) {
    DockNode* leaf = target.leafId != 0 ? DockNodeQuery::FindLeaf(root.get(), target.leafId) : nullptr;
    if (leaf == nullptr) {
        leaf = root.get();
        if (leaf->kind != DockNode::Kind::Leaf) {
            root = DockNodeFactory::MakeSplit(
                nodeIds.Next(),
                DockSplitAxis::Horizontal,
                0.5F,
                std::move(root),
                DockNodeFactory::MakeLeaf(nodeIds.Next(), { panelId }));
            return;
        }
    }

    if (target.kind == DockDropPreviewKind::StripMarker) {
        const std::uint32_t insertIndex =
            std::min(target.tabInsertionIndex, static_cast<std::uint32_t>(leaf->panels.size()));
        leaf->panels.insert(leaf->panels.begin() + static_cast<std::ptrdiff_t>(insertIndex), panelId);
    } else {
        leaf->panels.push_back(panelId);
    }
    leaf->activePanelId = panelId;
}

} // namespace kb::editor
