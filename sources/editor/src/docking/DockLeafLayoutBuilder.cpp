#include "docking/DockLeafLayoutBuilder.hpp"

#include "docking/DockGeometry.hpp"

#include <algorithm>

namespace kb::editor {

void DockLeafLayoutBuilder::Build(const DockNode& node, const DockRect& rect, DockLayout& layout, const DockLayoutBuildSettings& settings) const {
    DockLeafLayout leaf{
        .leafId = node.id,
        .frame = rect,
        .tabStrip = DockGeometry::PanelTabStrip(rect, settings.tabStripHeight),
        .content = DockGeometry::PanelContent(rect, settings.tabStripHeight),
        .activePanelId = node.activePanelId,
    };
    layout.leaves.push_back(leaf);

    int activeIndex = -1;
    for (std::size_t index = 0; index < node.panels.size(); ++index) {
        if (node.panels[index] == node.activePanelId) {
            activeIndex = static_cast<int>(index);
        }
    }
    const std::vector<int> tabWidths = DockGeometry::TabWidths(
        leaf.tabStrip.width, static_cast<int>(node.panels.size()), activeIndex, settings.tabMinWidth, settings.tabWidth);
    int tabX = leaf.tabStrip.x;

    for (std::size_t index = 0; index < node.panels.size(); ++index) {
        const std::uint32_t panelId = node.panels[index];
        const int resolvedTabWidth = tabWidths[index];
        layout.panels.push_back(DockPanelLayout{
            .panelId = panelId,
            .leafId = node.id,
            .frame = leaf.frame,
            .tabStrip = leaf.tabStrip,
            .tab = DockGeometry::MakeRect(tabX, leaf.tabStrip.y, std::min(resolvedTabWidth, leaf.tabStrip.x + leaf.tabStrip.width - tabX), settings.tabStripHeight),
            .content = leaf.content,
            .contentClip = leaf.content,
            .active = panelId == node.activePanelId,
        });
        tabX += resolvedTabWidth;
    }
}

} // namespace kb::editor
