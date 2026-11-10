#include "docking/EditorDockModelCommands.hpp"
#include <algorithm>
#include <utility>
#include <vector>

#include "docking/DefaultDockWorkspace.hpp"
#include "docking/DockLayoutSerializer.hpp"
#include "docking/DockLeafPanelOrder.hpp"
#include "docking/DockNodeFactory.hpp"
#include "docking/DockNodeQuery.hpp"
#include "docking/EditorDockModelQueries.hpp"
#include "docking/DockPanelDocking.hpp"
#include "docking/DockTreeMutation.hpp"
#include "docking/DockSplitterResizer.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] DockNode* FirstLeaf(DockNode* node) noexcept {
    if (node == nullptr) {
        return nullptr;
    }
    if (node->kind == DockNode::Kind::Leaf) {
        return node;
    }
    if (DockNode* first = FirstLeaf(node->first.get()); first != nullptr) {
        return first;
    }
    return FirstLeaf(node->second.get());
}

// A panel an editor extension registered at run time: it is in the workspace, but not in the
// layouts a project saves, because the next session may not have the extension.
[[nodiscard]] bool IsToolPanel(const DockPanel& panel) noexcept {
    return panel.kind == DockPanelKind::Generic && panel.id >= kFirstToolPanelId;
}

[[nodiscard]] std::vector<DockPanel> ToolPanels(const DockPanelCollection& panels) {
    std::vector<DockPanel> tools;
    for (const DockPanel& panel : panels.All()) {
        if (IsToolPanel(panel)) {
            tools.push_back(panel);
        }
    }
    return tools;
}

[[nodiscard]] std::unique_ptr<DockNode> CopyNode(const DockNode* node) {
    if (node == nullptr) {
        return {};
    }
    auto copy = std::make_unique<DockNode>();
    copy->kind = node->kind;
    copy->id = node->id;
    copy->axis = node->axis;
    copy->ratio = node->ratio;
    copy->panels = node->panels;
    copy->activePanelId = node->activePanelId;
    copy->first = CopyNode(node->first.get());
    copy->second = CopyNode(node->second.get());
    return copy;
}

} // namespace

EditorDockModelCommands::EditorDockModelCommands(DockPanelCollection& panels, std::unique_ptr<DockNode>& root, std::uint32_t& nextNodeId, std::uint32_t& maximizedLeafId, bool& drawerCollapsed) noexcept
    : panels_(panels)
    , root_(root)
    , nextNodeId_(nextNodeId)
    , maximizedLeafId_(maximizedLeafId)
    , drawerCollapsed_(drawerCollapsed) {}

bool EditorDockModelCommands::SetDrawerCollapsed(bool collapsed) noexcept {
    if (drawerCollapsed_ == collapsed) {
        return false;
    }
    drawerCollapsed_ = collapsed;
    return true;
}

void EditorDockModelCommands::ResetWorkspace() {
    std::vector<DockPanel> tools = ToolPanels(panels_);
    panels_.Reset(DefaultDockWorkspace{}.CreatePanels());
    nextNodeId_ = 1U;
    root_ = DefaultDockWorkspace{}.CreateRoot(nextNodeId_);
    for (DockPanel& tool : tools) {
        static_cast<void>(RegisterToolPanel(std::move(tool)));
    }
    maximizedLeafId_ = 0U;
}

bool EditorDockModelCommands::RestoreWorkspace(std::string_view tree) {
    std::vector<std::uint32_t> knownPanels;
    knownPanels.reserve(panels_.All().size());
    for (const DockPanel& panel : panels_.All()) {
        if (!IsToolPanel(panel)) {
            knownPanels.push_back(panel.id);
        }
    }

    std::uint32_t nextNodeId = 1U;
    std::unique_ptr<DockNode> restored = DockLayoutSerializer::Parse(tree, knownPanels, nextNodeId);
    if (restored == nullptr) {
        return false;
    }
    std::vector<DockPanel> tools = ToolPanels(panels_);
    root_ = std::move(restored);
    nextNodeId_ = nextNodeId;
    maximizedLeafId_ = 0U;
    // The saved tree decides what is open: a panel it places is visible even when the
    // default workspace starts it closed, and one it does not place - closed when it was
    // written, or added by a later build - stays hidden, so the caller can reopen or close
    // it from the saved session.
    for (const DockPanel& panel : panels_.All()) {
        if (DockPanel* restoredPanel = panels_.Find(panel.id); restoredPanel != nullptr) {
            restoredPanel->visible = DockNodeQuery::FindLeafContaining(root_.get(), panel.id) != nullptr;
            if (restoredPanel->visible && restoredPanel->area == DockArea::Floating) {
                restoredPanel->area = DockArea::Center;
            }
        }
    }
    for (DockPanel& tool : tools) {
        static_cast<void>(RegisterToolPanel(std::move(tool)));
    }
    return true;
}

std::string EditorDockModelCommands::SerializeWorkspace() const {
    std::unique_ptr<DockNode> saved = CopyNode(root_.get());
    for (const DockPanel& panel : panels_.All()) {
        if (IsToolPanel(panel)) {
            DockTreeMutation::RemovePanel(saved, panel.id);
        }
    }
    DockTreeMutation::PruneEmptyBranches(saved);
    return DockLayoutSerializer::Serialize(saved.get());
}

bool EditorDockModelCommands::RegisterToolPanel(DockPanel panel) {
    if (!IsToolPanel(panel) || panel.title.empty()) {
        return false;
    }
    if (const DockPanel* existing = panels_.Find(panel.id); existing != nullptr && !IsToolPanel(*existing)) {
        return false;
    }
    const std::uint32_t panelId = panel.id;
    std::vector<DockPanel> all = panels_.All();
    std::erase_if(all, [panelId](const DockPanel& candidate) { return candidate.id == panelId; });
    panel.detachable = false;
    all.push_back(std::move(panel));
    panels_.Reset(std::move(all));

    // A tool panel keeps the place it already has; a new one joins the group at the right, or
    // the first group there is, or becomes the workspace when nothing else is open.
    DockNode* leaf = DockNodeQuery::FindLeafContaining(root_.get(), panelId);
    if (leaf == nullptr) {
        for (const DockPanel& candidate : panels_.All()) {
            if (candidate.area == DockArea::Right && candidate.visible) {
                leaf = DockNodeQuery::FindLeafContaining(root_.get(), candidate.id);
                if (leaf != nullptr) {
                    break;
                }
            }
        }
        if (leaf == nullptr) {
            leaf = FirstLeaf(root_.get());
        }
        if (leaf == nullptr) {
            root_ = DockNodeFactory::MakeLeaf(NextNodeId(), { panelId });
            leaf = root_.get();
        } else {
            leaf->panels.push_back(panelId);
        }
    }
    DockPanel* stored = panels_.Find(panelId);
    stored->area = DockArea::Right;
    stored->visible = true;
    leaf->activePanelId = panelId;
    maximizedLeafId_ = 0;
    return true;
}

bool EditorDockModelCommands::UnregisterToolPanel(std::uint32_t id) {
    const DockPanel* panel = panels_.Find(id);
    if (panel == nullptr || !IsToolPanel(*panel)) {
        return false;
    }
    static_cast<void>(ClosePanel(id));
    std::vector<DockPanel> all = panels_.All();
    std::erase_if(all, [id](const DockPanel& candidate) { return candidate.id == id; });
    panels_.Reset(std::move(all));
    return true;
}

void EditorDockModelCommands::ActivatePanel(std::uint32_t panelId) {
    DockLeafPanelOrder::Activate(root_.get(), panelId);
    // Choosing a tab of the collapsed drawer opens it.
    const DockNode* leaf = DockNodeQuery::FindLeafContaining(root_.get(), panelId);
    if (drawerCollapsed_ && leaf != nullptr && leaf->id == EditorDockModelQueries::DrawerLeafId(panels_, root_.get())) {
        drawerCollapsed_ = false;
    }
}

bool EditorDockModelCommands::ClosePanel(std::uint32_t panelId) {
    DockPanel* panel = panels_.Find(panelId);
    const DockNode* leaf = DockNodeQuery::FindLeafContaining(root_.get(), panelId);
    if (panel == nullptr) return false;
    if (leaf == nullptr) {
        if (panel->area != DockArea::Floating || !panel->visible) return false;
        panel->visible = false;
        return true;
    }

    const std::uint32_t leafId = leaf->id;
    DockTreeMutation::RemovePanel(root_, panelId);
    DockTreeMutation::PruneEmptyBranches(root_);
    panel->visible = false;
    if (maximizedLeafId_ == leafId && DockNodeQuery::FindLeaf(root_.get(), leafId) == nullptr) {
        maximizedLeafId_ = 0;
    }
    return true;
}

void EditorDockModelCommands::ResizeSplitter(std::uint32_t nodeId, int mouseX, int mouseY, const DockLayout& layout) {
    DockSplitterResizer::Resize(root_.get(), nodeId, mouseX, mouseY, layout);
}

void EditorDockModelCommands::ReorderPanelInLeaf(std::uint32_t panelId, std::uint32_t leafId, std::uint32_t newIndex) {
    DockLeafPanelOrder::Reorder(root_.get(), panelId, leafId, newIndex);
}

void EditorDockModelCommands::UndockPanel(std::uint32_t panelId, DockRect floatingRect) {
    maximizedLeafId_ = 0;
    DockPanelDocking::Undock(panels_, root_, panelId, floatingRect);
    if (DockPanel* panel = panels_.Find(panelId); panel != nullptr && panel->area == DockArea::Floating) {
        panel->visible = true;
    }
}

void EditorDockModelCommands::DockPanelTo(std::uint32_t panelId, const DockDropPreview& target) {
    maximizedLeafId_ = 0;
    if (DockPanel* panel = panels_.Find(panelId); panel != nullptr) {
        panel->visible = true;
    }
    DockPanelDocking::Dock(panels_, root_, panelId, target, &EditorDockModelCommands::NextNodeIdCallback, this);
}

bool EditorDockModelCommands::ActivatePanelKind(DockPanelKind kind, DockArea fallbackArea) {
    DockPanel* panel = nullptr;
    for (const DockPanel& candidate : panels_.All()) {
        if (candidate.kind == kind) {
            panel = panels_.Find(candidate.id);
            break;
        }
    }
    if (panel == nullptr) {
        return false;
    }

    DockNode* leaf = DockNodeQuery::FindLeafContaining(root_.get(), panel->id);
    if (leaf == nullptr) {
        const DockArea targetArea = panel->area == DockArea::Floating ? fallbackArea : panel->area;
        for (const DockPanel& candidate : panels_.All()) {
            if (candidate.id == panel->id || !candidate.visible || candidate.area != targetArea) {
                continue;
            }
            leaf = DockNodeQuery::FindLeafContaining(root_.get(), candidate.id);
            if (leaf != nullptr) {
                break;
            }
        }
        if (leaf == nullptr) {
            leaf = FirstLeaf(root_.get());
        }
        if (leaf == nullptr) {
            root_ = DockNodeFactory::MakeLeaf(NextNodeId(), { panel->id });
            leaf = root_.get();
        } else {
            leaf->panels.push_back(panel->id);
        }
        panel->area = targetArea;
    }

    panel->visible = true;
    leaf->activePanelId = panel->id;
    maximizedLeafId_ = 0;
    // Opening a drawer panel from a menu or the status bar opens the drawer with it.
    if (leaf->id == EditorDockModelQueries::DrawerLeafId(panels_, root_.get())) {
        drawerCollapsed_ = false;
    }
    return true;
}

bool EditorDockModelCommands::SetPanelTitle(DockPanelKind kind, std::string title) {
    if (title.empty()) {
        return false;
    }
    for (const DockPanel& candidate : panels_.All()) {
        if (candidate.kind != kind) {
            continue;
        }
        DockPanel* panel = panels_.Find(candidate.id);
        if (panel == nullptr || panel->title == title) {
            return panel != nullptr;
        }
        panel->title = std::move(title);
        return true;
    }
    return false;
}

void EditorDockModelCommands::MoveFloatingPanel(std::uint32_t panelId, int x, int y) {
    panels_.MoveFloatingPanel(panelId, x, y);
}

void EditorDockModelCommands::ResizeFloatingPanel(std::uint32_t panelId, int width, int height) {
    panels_.ResizeFloatingPanel(panelId, width, height);
}

bool EditorDockModelCommands::ToggleMaximizedLeaf(std::uint32_t leafId) noexcept {
    if (leafId == 0) {
        return false;
    }
    if (maximizedLeafId_ == leafId) {
        maximizedLeafId_ = 0;
        return true;
    }
    if (DockNodeQuery::FindLeaf(root_.get(), leafId) == nullptr) {
        return false;
    }
    maximizedLeafId_ = leafId;
    return true;
}

bool EditorDockModelCommands::RestoreMaximizedLeaf() noexcept {
    if (maximizedLeafId_ == 0) {
        return false;
    }
    maximizedLeafId_ = 0;
    return true;
}

std::uint32_t EditorDockModelCommands::NextNodeId() noexcept {
    return nextNodeId_++;
}

std::uint32_t EditorDockModelCommands::NextNodeIdCallback(void* context) noexcept {
    return static_cast<EditorDockModelCommands*>(context)->NextNodeId();
}

} // namespace kb::editor
