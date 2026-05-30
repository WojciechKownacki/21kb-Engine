#include "docking/EditorDockModel.hpp"

#include "docking/DefaultDockWorkspace.hpp"
#include "docking/DockLeafPanelOrder.hpp"
#include "docking/DockModelQueries.hpp"
#include "docking/DockPanelDocking.hpp"
#include "docking/DockSplitterResizer.hpp"

namespace kb::editor {

EditorDockModel::EditorDockModel()
    : panels_(DefaultDockWorkspace{}.CreatePanels()) {
    root_ = DefaultDockWorkspace{}.CreateRoot(nextNodeId_);
}

EditorDockModel::~EditorDockModel() = default;

const std::vector<DockPanel>& EditorDockModel::Panels() const noexcept {
    return panels_.All();
}

std::vector<DockPanel> EditorDockModel::PanelsInArea(DockArea area) const {
    return panels_.InArea(area);
}

const DockPanel* EditorDockModel::FindPanel(std::uint32_t panelId) const noexcept {
    return panels_.Find(panelId);
}

DockPanel* EditorDockModel::FindPanel(std::uint32_t panelId) noexcept {
    return panels_.Find(panelId);
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
    return DockModelQueries::BuildLayout(root_.get(), clientWidth, clientHeight, menuHeight, toolbarHeight, tabStripHeight, tabMinWidth, tabWidth, splitterSize, panelPadding);
}

DockHit EditorDockModel::HitTest(const DockLayout& layout, int x, int y) const {
    return DockModelQueries::HitTest(layout, x, y);
}

std::optional<DockDropPreview> EditorDockModel::ResolveDropPreview(const DockLayout& layout, int x, int y) const {
    return DockModelQueries::ResolveDropPreview(layout, x, y);
}

void EditorDockModel::ActivatePanel(std::uint32_t panelId) {
    DockLeafPanelOrder::Activate(root_.get(), panelId);
}

void EditorDockModel::ResizeSplitter(std::uint32_t nodeId, int mouseX, int mouseY, const DockLayout& layout) {
    DockSplitterResizer::Resize(root_.get(), nodeId, mouseX, mouseY, layout);
}

void EditorDockModel::ReorderPanelInLeaf(std::uint32_t panelId, std::uint32_t leafId, std::uint32_t newIndex) {
    DockLeafPanelOrder::Reorder(root_.get(), panelId, leafId, newIndex);
}

std::uint32_t EditorDockModel::PanelCountInLeaf(std::uint32_t leafId) const noexcept {
    return DockLeafPanelOrder::Count(root_.get(), leafId);
}

void EditorDockModel::UndockPanel(std::uint32_t panelId, DockRect floatingRect) {
    DockPanelDocking::Undock(panels_, root_, panelId, floatingRect);
}

void EditorDockModel::DockPanelTo(std::uint32_t panelId, const DockDropPreview& target) {
    DockPanelDocking::Dock(panels_, root_, panelId, target, &EditorDockModel::NextNodeIdCallback, this);
}

void EditorDockModel::MoveFloatingPanel(std::uint32_t panelId, int x, int y) {
    panels_.MoveFloatingPanel(panelId, x, y);
}

void EditorDockModel::ResizeFloatingPanel(std::uint32_t panelId, int width, int height) {
    panels_.ResizeFloatingPanel(panelId, width, height);
}

std::uint32_t EditorDockModel::NextNodeId() noexcept {
    return nextNodeId_++;
}

std::uint32_t EditorDockModel::NextNodeIdCallback(void* context) noexcept {
    return static_cast<EditorDockModel*>(context)->NextNodeId();
}

} // namespace kb::editor
