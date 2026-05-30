#include "docking/EditorDockModelQueries.hpp"

#include "docking/DockLeafPanelOrder.hpp"
#include "docking/DockModelQueries.hpp"

namespace kb::editor {

EditorDockModelQueries::EditorDockModelQueries(const DockPanelCollection& panels, const DockNode* root) noexcept
    : panels_(panels)
    , root_(root) {}

const std::vector<DockPanel>& EditorDockModelQueries::Panels() const noexcept {
    return panels_.All();
}

std::vector<DockPanel> EditorDockModelQueries::PanelsInArea(DockArea area) const {
    return panels_.InArea(area);
}

const DockPanel* EditorDockModelQueries::FindPanel(std::uint32_t panelId) const noexcept {
    return panels_.Find(panelId);
}

DockLayout EditorDockModelQueries::BuildLayout(
    int clientWidth,
    int clientHeight,
    int menuHeight,
    int toolbarHeight,
    int tabStripHeight,
    int tabMinWidth,
    int tabWidth,
    int splitterSize,
    int panelPadding) const {
    return DockModelQueries::BuildLayout(root_, clientWidth, clientHeight, menuHeight, toolbarHeight, tabStripHeight, tabMinWidth, tabWidth, splitterSize, panelPadding);
}

DockHit EditorDockModelQueries::HitTest(const DockLayout& layout, int x, int y) const {
    return DockModelQueries::HitTest(layout, x, y);
}

std::optional<DockDropPreview> EditorDockModelQueries::ResolveDropPreview(const DockLayout& layout, int x, int y) const {
    return DockModelQueries::ResolveDropPreview(layout, x, y);
}

std::uint32_t EditorDockModelQueries::PanelCountInLeaf(std::uint32_t leafId) const noexcept {
    return DockLeafPanelOrder::Count(root_, leafId);
}

} // namespace kb::editor
