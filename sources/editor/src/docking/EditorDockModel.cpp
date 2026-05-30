#include "docking/EditorDockModel.hpp"

#include "docking/DefaultDockWorkspace.hpp"
#include "docking/DockDropPreviewResolver.hpp"
#include "docking/DockGeometry.hpp"
#include "docking/DockHitTester.hpp"
#include "docking/DockLayoutBuilder.hpp"
#include "docking/DockNodeFactory.hpp"

#include <algorithm>
#include <cmath>

namespace kb::editor {
namespace {

constexpr float kRootSplitRatio = 0.25F;
constexpr float kLeafSplitRatio = 0.50F;

} // namespace

EditorDockModel::EditorDockModel()
    : panels_(DefaultDockWorkspace{}.CreatePanels()) {
    root_ = DefaultDockWorkspace{}.CreateRoot(nextNodeId_);
}

EditorDockModel::~EditorDockModel() = default;

const std::vector<DockPanel>& EditorDockModel::Panels() const noexcept {
    return panels_;
}

std::vector<DockPanel> EditorDockModel::PanelsInArea(DockArea area) const {
    std::vector<DockPanel> result;
    for (const DockPanel& panel : panels_) {
        if (panel.visible && panel.area == area) {
            result.push_back(panel);
        }
    }
    return result;
}

const DockPanel* EditorDockModel::FindPanel(std::uint32_t panelId) const noexcept {
    for (const DockPanel& panel : panels_) {
        if (panel.id == panelId) {
            return &panel;
        }
    }
    return nullptr;
}

DockPanel* EditorDockModel::FindPanel(std::uint32_t panelId) noexcept {
    return const_cast<DockPanel*>(static_cast<const EditorDockModel&>(*this).FindPanel(panelId));
}

DockLayout EditorDockModel::BuildLayout(
    int clientWidth,
    int clientHeight,
    int menuHeight,
    int toolbarHeight,
    int tabStripHeight,
    int tabMinWidth,
    int tabWidth,
    int splitterSize,
    int panelPadding) const {
    return DockLayoutBuilder{}.Build(root_.get(), clientWidth, clientHeight, menuHeight, toolbarHeight, tabStripHeight, tabMinWidth, tabWidth, splitterSize, panelPadding);
}

DockHit EditorDockModel::HitTest(const DockLayout& layout, int x, int y) const {
    return DockHitTester{}.HitTest(layout, x, y);
}

std::optional<DockDropPreview> EditorDockModel::ResolveDropPreview(const DockLayout& layout, int x, int y) const {
    return DockDropPreviewResolver{}.Resolve(layout, x, y);
}

void EditorDockModel::ActivatePanel(std::uint32_t panelId) {
    if (DockNode* leaf = FindLeafContaining(panelId); leaf != nullptr) {
        leaf->activePanelId = panelId;
    }
}

void EditorDockModel::ResizeSplitter(std::uint32_t nodeId, int mouseX, int mouseY, const DockLayout& layout) {
    DockNode* node = FindNode(nodeId);
    if (node == nullptr || node->kind != DockNode::Kind::Split) {
        return;
    }
    const auto it = std::find_if(layout.splitters.begin(), layout.splitters.end(), [nodeId](const DockSplitterLayout& splitter) {
        return splitter.nodeId == nodeId;
    });
    if (it == layout.splitters.end()) {
        return;
    }

    if (node->axis == DockSplitAxis::Horizontal) {
        node->ratio = DockGeometry::ClampRatio(static_cast<float>(mouseX - it->container.x) / static_cast<float>(std::max(1, it->container.width)));
    } else {
        node->ratio = DockGeometry::ClampRatio(static_cast<float>(mouseY - it->container.y) / static_cast<float>(std::max(1, it->container.height)));
    }
}

void EditorDockModel::ReorderPanelInLeaf(std::uint32_t panelId, std::uint32_t leafId, std::uint32_t newIndex) {
    DockNode* leaf = FindLeaf(leafId);
    if (leaf == nullptr) {
        return;
    }
    auto it = std::find(leaf->panels.begin(), leaf->panels.end(), panelId);
    if (it == leaf->panels.end()) {
        return;
    }

    const std::uint32_t oldIndex = static_cast<std::uint32_t>(std::distance(leaf->panels.begin(), it));
    const std::uint32_t lastIndex = leaf->panels.empty() ? 0U : static_cast<std::uint32_t>(leaf->panels.size() - 1U);
    newIndex = std::min(newIndex, lastIndex);
    if (oldIndex == newIndex) {
        return;
    }

    const std::uint32_t value = *it;
    leaf->panels.erase(it);
    leaf->panels.insert(leaf->panels.begin() + static_cast<std::ptrdiff_t>(newIndex), value);
    leaf->activePanelId = panelId;
}

std::uint32_t EditorDockModel::PanelCountInLeaf(std::uint32_t leafId) const noexcept {
    const DockNode* leaf = FindLeaf(leafId);
    return leaf == nullptr ? 0U : static_cast<std::uint32_t>(leaf->panels.size());
}

void EditorDockModel::UndockPanel(std::uint32_t panelId, DockRect floatingRect) {
    DockPanel* panel = FindPanel(panelId);
    if (panel == nullptr || !panel->detachable) {
        return;
    }

    RemovePanelFromDockTree(panelId);
    PruneEmptyNodes();
    panel->area = DockArea::Floating;
    panel->floatingRect = floatingRect;
}

void EditorDockModel::DockPanelTo(std::uint32_t panelId, const DockDropPreview& target) {
    DockPanel* panel = FindPanel(panelId);
    if (panel == nullptr) {
        return;
    }

    RemovePanelFromDockTree(panelId);
    PruneEmptyNodes();
    panel->area = AreaForZone(target.zone);

    if (root_ == nullptr) {
        root_ = DockNodeFactory::MakeLeaf(NextNodeId(), { panelId });
        return;
    }

    if (target.zone == DockDropZone::Center) {
        DockNode* leaf = target.leafId != 0 ? FindLeaf(target.leafId) : nullptr;
        if (leaf == nullptr) {
            leaf = root_.get();
            if (leaf->kind != DockNode::Kind::Leaf) {
                root_ = DockNodeFactory::MakeSplit(NextNodeId(), DockSplitAxis::Horizontal, 0.5F, std::move(root_), DockNodeFactory::MakeLeaf(NextNodeId(), { panelId }));
                return;
            }
        }
        leaf->panels.push_back(panelId);
        leaf->activePanelId = panelId;
        return;
    }

    const DockSplitAxis axis = (target.zone == DockDropZone::Left || target.zone == DockDropZone::Right) ? DockSplitAxis::Horizontal : DockSplitAxis::Vertical;
    const bool droppedFirst = target.zone == DockDropZone::Left || target.zone == DockDropZone::Top;
    const float ratio = target.leafId == 0 ? (droppedFirst ? kRootSplitRatio : 1.0F - kRootSplitRatio) : kLeafSplitRatio;
    auto dropped = DockNodeFactory::MakeLeaf(NextNodeId(), { panelId });

    if (target.leafId == 0) {
        root_ = droppedFirst
                    ? DockNodeFactory::MakeSplit(NextNodeId(), axis, ratio, std::move(dropped), std::move(root_))
                    : DockNodeFactory::MakeSplit(NextNodeId(), axis, ratio, std::move(root_), std::move(dropped));
        return;
    }

    std::unique_ptr<DockNode>* slot = FindNodeSlot(target.leafId);
    if (slot == nullptr || *slot == nullptr) {
        root_ = droppedFirst
                    ? DockNodeFactory::MakeSplit(NextNodeId(), axis, ratio, std::move(dropped), std::move(root_))
                    : DockNodeFactory::MakeSplit(NextNodeId(), axis, ratio, std::move(root_), std::move(dropped));
        return;
    }

    auto oldLeaf = std::move(*slot);
    *slot = droppedFirst
                ? DockNodeFactory::MakeSplit(NextNodeId(), axis, ratio, std::move(dropped), std::move(oldLeaf))
                : DockNodeFactory::MakeSplit(NextNodeId(), axis, ratio, std::move(oldLeaf), std::move(dropped));
}

void EditorDockModel::MoveFloatingPanel(std::uint32_t panelId, int x, int y) {
    if (DockPanel* panel = FindPanel(panelId); panel != nullptr) {
        panel->floatingRect.x = x;
        panel->floatingRect.y = y;
    }
}

void EditorDockModel::ResizeFloatingPanel(std::uint32_t panelId, int width, int height) {
    if (DockPanel* panel = FindPanel(panelId); panel != nullptr) {
        panel->floatingRect.width = std::max(260, width);
        panel->floatingRect.height = std::max(180, height);
    }
}

DockNode* EditorDockModel::FindNode(std::uint32_t nodeId) noexcept {
    return const_cast<DockNode*>(static_cast<const EditorDockModel&>(*this).FindNode(nodeId));
}

const DockNode* EditorDockModel::FindNode(std::uint32_t nodeId) const noexcept {
    const auto visit = [](const DockNode* node, std::uint32_t id, const auto& self) -> const DockNode* {
        if (node == nullptr) {
            return nullptr;
        }
        if (node->id == id) {
            return node;
        }
        if (const DockNode* found = self(node->first.get(), id, self); found != nullptr) {
            return found;
        }
        return self(node->second.get(), id, self);
    };
    return visit(root_.get(), nodeId, visit);
}

DockNode* EditorDockModel::FindLeafContaining(std::uint32_t panelId) noexcept {
    return const_cast<DockNode*>(static_cast<const EditorDockModel&>(*this).FindLeafContaining(panelId));
}

const DockNode* EditorDockModel::FindLeafContaining(std::uint32_t panelId) const noexcept {
    const auto visit = [](const DockNode* node, std::uint32_t id, const auto& self) -> const DockNode* {
        if (node == nullptr) {
            return nullptr;
        }
        if (node->kind == DockNode::Kind::Leaf && std::find(node->panels.begin(), node->panels.end(), id) != node->panels.end()) {
            return node;
        }
        if (const DockNode* found = self(node->first.get(), id, self); found != nullptr) {
            return found;
        }
        return self(node->second.get(), id, self);
    };
    return visit(root_.get(), panelId, visit);
}

DockNode* EditorDockModel::FindLeaf(std::uint32_t leafId) noexcept {
    DockNode* node = FindNode(leafId);
    return node != nullptr && node->kind == DockNode::Kind::Leaf ? node : nullptr;
}

const DockNode* EditorDockModel::FindLeaf(std::uint32_t leafId) const noexcept {
    const DockNode* node = FindNode(leafId);
    return node != nullptr && node->kind == DockNode::Kind::Leaf ? node : nullptr;
}

std::unique_ptr<DockNode>* EditorDockModel::FindNodeSlot(std::uint32_t nodeId) noexcept {
    return root_ != nullptr && root_->id == nodeId ? &root_ : FindNodeSlotRecursive(root_, nodeId);
}

std::unique_ptr<DockNode>* EditorDockModel::FindNodeSlotRecursive(std::unique_ptr<DockNode>& node, std::uint32_t nodeId) noexcept {
    if (node == nullptr || node->kind != DockNode::Kind::Split) {
        return nullptr;
    }
    if (node->first != nullptr && node->first->id == nodeId) {
        return &node->first;
    }
    if (node->second != nullptr && node->second->id == nodeId) {
        return &node->second;
    }
    if (std::unique_ptr<DockNode>* slot = FindNodeSlotRecursive(node->first, nodeId); slot != nullptr) {
        return slot;
    }
    return FindNodeSlotRecursive(node->second, nodeId);
}

std::uint32_t EditorDockModel::NextNodeId() noexcept {
    return nextNodeId_++;
}

void EditorDockModel::RemovePanelFromDockTree(std::uint32_t panelId) {
    DockNode* leaf = FindLeafContaining(panelId);
    if (leaf == nullptr) {
        return;
    }
    leaf->panels.erase(std::remove(leaf->panels.begin(), leaf->panels.end(), panelId), leaf->panels.end());
    if (leaf->activePanelId == panelId) {
        leaf->activePanelId = leaf->panels.empty() ? 0 : leaf->panels.front();
    }
}

void EditorDockModel::PruneEmptyNodes() {
    if (root_ == nullptr) {
        return;
    }
    if (PruneEmptyNodesRecursive(root_)) {
        root_.reset();
    }
}

bool EditorDockModel::PruneEmptyNodesRecursive(std::unique_ptr<DockNode>& node) {
    if (node == nullptr) {
        return true;
    }
    if (node->kind == DockNode::Kind::Leaf) {
        return node->panels.empty();
    }

    const bool firstEmpty = PruneEmptyNodesRecursive(node->first);
    if (firstEmpty) {
        node->first.reset();
    }
    const bool secondEmpty = PruneEmptyNodesRecursive(node->second);
    if (secondEmpty) {
        node->second.reset();
    }

    if (node->first == nullptr && node->second == nullptr) {
        return true;
    }
    if (node->first == nullptr) {
        node = std::move(node->second);
    } else if (node->second == nullptr) {
        node = std::move(node->first);
    }
    return false;
}

void EditorDockModel::SetPanelArea(std::uint32_t panelId, DockArea area) {
    if (DockPanel* panel = FindPanel(panelId); panel != nullptr) {
        panel->area = area;
    }
}

DockArea EditorDockModel::AreaForZone(DockDropZone zone) const noexcept {
    switch (zone) {
    case DockDropZone::Left:
        return DockArea::Left;
    case DockDropZone::Right:
        return DockArea::Right;
    case DockDropZone::Bottom:
        return DockArea::Bottom;
    case DockDropZone::Top:
    case DockDropZone::Center:
    case DockDropZone::None:
    default:
        return DockArea::Center;
    }
}

} // namespace kb::editor
