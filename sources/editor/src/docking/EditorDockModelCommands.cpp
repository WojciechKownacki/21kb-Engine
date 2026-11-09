#include "docking/EditorDockModelCommands.hpp"
#include <algorithm>

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

bool ToolPanel(const DockPanel& p) {return p.kind==DockPanelKind::Generic&&p.id>=1000000U;}
std::unique_ptr<DockNode> CopyNode(const DockNode* n) {
    if(!n) return {};auto copy=std::make_unique<DockNode>();copy->kind=n->kind;copy->id=n->id;copy->axis=n->axis;copy->ratio=n->ratio;copy->panels=n->panels;copy->activePanelId=n->activePanelId;copy->first=CopyNode(n->first.get());copy->second=CopyNode(n->second.get());return copy;
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
    std::vector<DockPanel> tools;for(const auto& p:panels_.All()) if(ToolPanel(p)) tools.push_back(p);
    panels_.Reset(DefaultDockWorkspace{}.CreatePanels());
    nextNodeId_ = 1U;
    root_ = DefaultDockWorkspace{}.CreateRoot(nextNodeId_);
    for(auto& p:tools) static_cast<void>(RegisterToolPanel(std::move(p)));
    maximizedLeafId_ = 0U;
}

bool EditorDockModelCommands::RestoreWorkspace(std::string_view tree) {
    std::vector<std::uint32_t> knownPanels;
    knownPanels.reserve(panels_.All().size());
    for (const DockPanel& panel : panels_.All()) {
        if(!ToolPanel(panel)) knownPanels.push_back(panel.id);
    }

    std::uint32_t nextNodeId = 1U;
    std::unique_ptr<DockNode> restored = DockLayoutSerializer::Parse(tree, knownPanels, nextNodeId);
    if (restored == nullptr) {
        return false;
    }
    std::vector<DockPanel> tools;for(const auto& p:panels_.All()) if(ToolPanel(p)) tools.push_back(p);
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
    for(auto& p:tools) static_cast<void>(RegisterToolPanel(std::move(p)));
    return true;
}

std::string EditorDockModelCommands::SerializeWorkspace() const {
    auto saved=CopyNode(root_.get());
    for(const auto& p:panels_.All()) if(ToolPanel(p)) DockTreeMutation::RemovePanel(saved,p.id);
    DockTreeMutation::PruneEmptyBranches(saved);
    return DockLayoutSerializer::Serialize(saved.get());
}

bool EditorDockModelCommands::RegisterToolPanel(DockPanel panel) {
    if(!ToolPanel(panel)||panel.title.empty()) return false;
    if(const auto* existing=panels_.Find(panel.id);existing&&!ToolPanel(*existing)) return false;
    auto all=panels_.All();std::erase_if(all,[&](const auto& p){return p.id==panel.id;});panel.detachable=false;all.push_back(panel);panels_.Reset(std::move(all));
    auto* leaf=DockNodeQuery::FindLeafContaining(root_.get(),panel.id);
    if(!leaf) {for(const auto& candidate:panels_.All()) if(candidate.area==DockArea::Right&&candidate.visible) {leaf=DockNodeQuery::FindLeafContaining(root_.get(),candidate.id);if(leaf) break;}
        if(!leaf) leaf=FirstLeaf(root_.get());
        if(!leaf) {root_=DockNodeFactory::MakeLeaf(NextNodeId(),{panel.id});leaf=root_.get();}else leaf->panels.push_back(panel.id);
    }
    auto* stored=panels_.Find(panel.id);stored->area=DockArea::Right;stored->visible=true;leaf->activePanelId=panel.id;maximizedLeafId_=0;return true;
}
bool EditorDockModelCommands::UnregisterToolPanel(std::uint32_t id) {
    const auto* panel=panels_.Find(id);if(!panel||!ToolPanel(*panel)) return false;
    static_cast<void>(ClosePanel(id));auto all=panels_.All();std::erase_if(all,[&](const auto& p){return p.id==id;});panels_.Reset(std::move(all));return true;
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
