#include "docking/DockTreeMutation.hpp"

#include "docking/DockNodeFactory.hpp"
#include "docking/DockNodeQuery.hpp"
#include "docking/DockTreePruner.hpp"

#include <algorithm>
#include <utility>

namespace kb::editor {
namespace {

constexpr float kRootSplitRatio = 0.25F;
constexpr float kLeafSplitRatio = 0.50F;

} // namespace

void DockTreeMutation::RemovePanel(std::unique_ptr<DockNode>& root, std::uint32_t panelId) noexcept {
    DockNode* leaf = DockNodeQuery::FindLeafContaining(root.get(), panelId);
    if (leaf == nullptr) {
        return;
    }

    leaf->panels.erase(std::remove(leaf->panels.begin(), leaf->panels.end(), panelId), leaf->panels.end());
    if (leaf->activePanelId == panelId) {
        leaf->activePanelId = leaf->panels.empty() ? 0 : leaf->panels.front();
    }
}

void DockTreeMutation::PruneEmptyBranches(std::unique_ptr<DockNode>& root) noexcept {
    DockTreePruner::PruneEmptyBranches(root);
}

void DockTreeMutation::DockPanel(std::unique_ptr<DockNode>& root, std::uint32_t panelId, const DockDropPreview& target, NextNodeIdFn nextNodeId, void* context) {
    if (root == nullptr) {
        root = DockNodeFactory::MakeLeaf(NextNodeId(nextNodeId, context), { panelId });
        return;
    }

    if (target.zone == DockDropZone::Center) {
        DockPanelToCenter(root, panelId, target, nextNodeId, context);
        return;
    }

    DockPanelToSplit(root, panelId, target, nextNodeId, context);
}

std::uint32_t DockTreeMutation::NextNodeId(NextNodeIdFn nextNodeId, void* context) noexcept {
    return nextNodeId == nullptr ? 0U : nextNodeId(context);
}

DockSplitAxis DockTreeMutation::AxisForZone(DockDropZone zone) noexcept {
    return (zone == DockDropZone::Left || zone == DockDropZone::Right) ? DockSplitAxis::Horizontal : DockSplitAxis::Vertical;
}

bool DockTreeMutation::IsDroppedFirst(DockDropZone zone) noexcept {
    return zone == DockDropZone::Left || zone == DockDropZone::Top;
}

float DockTreeMutation::RatioForTarget(const DockDropPreview& target) noexcept {
    const bool droppedFirst = IsDroppedFirst(target.zone);
    return target.leafId == 0 ? (droppedFirst ? kRootSplitRatio : 1.0F - kRootSplitRatio) : kLeafSplitRatio;
}

void DockTreeMutation::DockPanelToCenter(std::unique_ptr<DockNode>& root, std::uint32_t panelId, const DockDropPreview& target, NextNodeIdFn nextNodeId, void* context) {
    DockNode* leaf = target.leafId != 0 ? DockNodeQuery::FindLeaf(root.get(), target.leafId) : nullptr;
    if (leaf == nullptr) {
        leaf = root.get();
        if (leaf->kind != DockNode::Kind::Leaf) {
            root = DockNodeFactory::MakeSplit(
                NextNodeId(nextNodeId, context),
                DockSplitAxis::Horizontal,
                0.5F,
                std::move(root),
                DockNodeFactory::MakeLeaf(NextNodeId(nextNodeId, context), { panelId }));
            return;
        }
    }

    leaf->panels.push_back(panelId);
    leaf->activePanelId = panelId;
}

void DockTreeMutation::DockPanelToSplit(std::unique_ptr<DockNode>& root, std::uint32_t panelId, const DockDropPreview& target, NextNodeIdFn nextNodeId, void* context) {
    const DockSplitAxis axis = AxisForZone(target.zone);
    const bool droppedFirst = IsDroppedFirst(target.zone);
    const float ratio = RatioForTarget(target);
    auto dropped = DockNodeFactory::MakeLeaf(NextNodeId(nextNodeId, context), { panelId });

    if (target.leafId == 0) {
        AttachToRoot(root, std::move(dropped), axis, ratio, droppedFirst, nextNodeId, context);
        return;
    }

    std::unique_ptr<DockNode>* slot = DockNodeQuery::FindNodeSlot(root, target.leafId);
    if (slot == nullptr || *slot == nullptr) {
        AttachToRoot(root, std::move(dropped), axis, ratio, droppedFirst, nextNodeId, context);
        return;
    }

    auto oldLeaf = std::move(*slot);
    *slot = droppedFirst
                ? DockNodeFactory::MakeSplit(NextNodeId(nextNodeId, context), axis, ratio, std::move(dropped), std::move(oldLeaf))
                : DockNodeFactory::MakeSplit(NextNodeId(nextNodeId, context), axis, ratio, std::move(oldLeaf), std::move(dropped));
}

void DockTreeMutation::AttachToRoot(
    std::unique_ptr<DockNode>& root,
    std::unique_ptr<DockNode> dropped,
    DockSplitAxis axis,
    float ratio,
    bool droppedFirst,
    NextNodeIdFn nextNodeId,
    void* context) {
    root = droppedFirst
                ? DockNodeFactory::MakeSplit(NextNodeId(nextNodeId, context), axis, ratio, std::move(dropped), std::move(root))
                : DockNodeFactory::MakeSplit(NextNodeId(nextNodeId, context), axis, ratio, std::move(root), std::move(dropped));
}

} // namespace kb::editor
