#include "docking/EditorDockModelCommands.hpp"

#include "docking/DockLeafPanelOrder.hpp"
#include "docking/DockPanelDocking.hpp"
#include "docking/DockSplitterResizer.hpp"

namespace kb::editor {

EditorDockModelCommands::EditorDockModelCommands(DockPanelCollection& panels, std::unique_ptr<DockNode>& root, std::uint32_t& nextNodeId) noexcept
    : panels_(panels)
    , root_(root)
    , nextNodeId_(nextNodeId) {}

void EditorDockModelCommands::ActivatePanel(std::uint32_t panelId) {
    DockLeafPanelOrder::Activate(root_.get(), panelId);
}

void EditorDockModelCommands::ResizeSplitter(std::uint32_t nodeId, int mouseX, int mouseY, const DockLayout& layout) {
    DockSplitterResizer::Resize(root_.get(), nodeId, mouseX, mouseY, layout);
}

void EditorDockModelCommands::ReorderPanelInLeaf(std::uint32_t panelId, std::uint32_t leafId, std::uint32_t newIndex) {
    DockLeafPanelOrder::Reorder(root_.get(), panelId, leafId, newIndex);
}

void EditorDockModelCommands::UndockPanel(std::uint32_t panelId, DockRect floatingRect) {
    DockPanelDocking::Undock(panels_, root_, panelId, floatingRect);
}

void EditorDockModelCommands::DockPanelTo(std::uint32_t panelId, const DockDropPreview& target) {
    DockPanelDocking::Dock(panels_, root_, panelId, target, &EditorDockModelCommands::NextNodeIdCallback, this);
}

void EditorDockModelCommands::MoveFloatingPanel(std::uint32_t panelId, int x, int y) {
    panels_.MoveFloatingPanel(panelId, x, y);
}

void EditorDockModelCommands::ResizeFloatingPanel(std::uint32_t panelId, int width, int height) {
    panels_.ResizeFloatingPanel(panelId, width, height);
}

std::uint32_t EditorDockModelCommands::NextNodeId() noexcept {
    return nextNodeId_++;
}

std::uint32_t EditorDockModelCommands::NextNodeIdCallback(void* context) noexcept {
    return static_cast<EditorDockModelCommands*>(context)->NextNodeId();
}

} // namespace kb::editor
