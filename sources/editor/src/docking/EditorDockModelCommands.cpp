#include "docking/EditorDockModelCommands.hpp"

#include "docking/DockLeafPanelOrder.hpp"
#include "docking/DockNodeQuery.hpp"
#include "docking/DockPanelDocking.hpp"
#include "docking/DockTreeMutation.hpp"
#include "docking/DockSplitterResizer.hpp"

namespace kb::editor {

EditorDockModelCommands::EditorDockModelCommands(DockPanelCollection& panels, std::unique_ptr<DockNode>& root, std::uint32_t& nextNodeId, std::uint32_t& maximizedLeafId) noexcept
    : panels_(panels)
    , root_(root)
    , nextNodeId_(nextNodeId)
    , maximizedLeafId_(maximizedLeafId) {}

void EditorDockModelCommands::ActivatePanel(std::uint32_t panelId) {
    DockLeafPanelOrder::Activate(root_.get(), panelId);
}

bool EditorDockModelCommands::ClosePanel(std::uint32_t panelId) {
    DockPanel* panel = panels_.Find(panelId);
    const DockNode* leaf = DockNodeQuery::FindLeafContaining(root_.get(), panelId);
    if (panel == nullptr || leaf == nullptr) {
        return false;
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
}

void EditorDockModelCommands::DockPanelTo(std::uint32_t panelId, const DockDropPreview& target) {
    maximizedLeafId_ = 0;
    if (DockPanel* panel = panels_.Find(panelId); panel != nullptr) {
        panel->visible = true;
    }
    DockPanelDocking::Dock(panels_, root_, panelId, target, &EditorDockModelCommands::NextNodeIdCallback, this);
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
