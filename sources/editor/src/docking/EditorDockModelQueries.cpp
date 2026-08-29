#include "docking/EditorDockModelQueries.hpp"

#include "docking/DockGeometry.hpp"
#include "docking/DockLeafPanelOrder.hpp"
#include "docking/DockModelQueries.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

[[nodiscard]] DockRect MakeRect(int x, int y, int width, int height) noexcept {
    return DockRect{
        .x = x,
        .y = y,
        .width = std::max(0, width),
        .height = std::max(0, height),
    };
}

[[nodiscard]] DockLayout MaximizeLeafLayout(const DockLayout& base, std::uint32_t leafId, int tabStripHeight, int tabMinWidth, int tabWidth) {
    DockLayout maximized{};
    maximized.menu = base.menu;
    maximized.toolbar = base.toolbar;
    maximized.workspace = base.workspace;
    if (base.workspace.Empty()) {
        return maximized;
    }

    DockLeafLayout leaf{
        .leafId = leafId,
        .frame = base.workspace,
        .tabStrip = DockGeometry::PanelTabStrip(base.workspace, tabStripHeight),
        .content = DockGeometry::PanelContent(base.workspace, tabStripHeight),
        .activePanelId = 0,
    };

    int panelCount = 0;
    for (const DockPanelLayout& panel : base.panels) {
        if (panel.leafId == leafId) {
            ++panelCount;
            if (panel.active) {
                leaf.activePanelId = panel.panelId;
            }
        }
    }
    if (panelCount <= 0) {
        return maximized;
    }

    maximized.leaves.push_back(leaf);
    const int maxTabWidth = std::max(1, tabWidth);
    const int minTabWidth = std::clamp(tabMinWidth, 1, maxTabWidth);
    const int resolvedTabWidth = std::clamp(leaf.tabStrip.width / std::max(1, panelCount), minTabWidth, maxTabWidth);
    int tabX = leaf.tabStrip.x;

    for (const DockPanelLayout& panel : base.panels) {
        if (panel.leafId != leafId) {
            continue;
        }
        maximized.panels.push_back(DockPanelLayout{
            .panelId = panel.panelId,
            .leafId = leafId,
            .frame = leaf.frame,
            .tabStrip = leaf.tabStrip,
            .tab = MakeRect(tabX, leaf.tabStrip.y, std::min(resolvedTabWidth, leaf.tabStrip.x + leaf.tabStrip.width - tabX), tabStripHeight),
            .content = leaf.content,
            .contentClip = leaf.content,
            .active = panel.active,
        });
        tabX += resolvedTabWidth;
    }

    return maximized;
}

} // namespace

EditorDockModelQueries::EditorDockModelQueries(const DockPanelCollection& panels, const DockNode* root, std::uint32_t maximizedLeafId) noexcept
    : panels_(panels)
    , root_(root)
    , maximizedLeafId_(maximizedLeafId) {}

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
    int splitterSize) const {
    DockLayout layout = DockModelQueries::BuildLayout(root_, clientWidth, clientHeight, menuHeight, toolbarHeight, tabStripHeight, tabMinWidth, tabWidth, splitterSize);
    if (maximizedLeafId_ == 0) {
        return layout;
    }
    return MaximizeLeafLayout(layout, maximizedLeafId_, tabStripHeight, tabMinWidth, tabWidth);
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
