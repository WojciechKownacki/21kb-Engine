#include "docking/DockLeafLayoutBuilder.hpp"

#include "docking/DockGeometry.hpp"

#include <algorithm>

namespace kb::editor {

void DockLeafLayoutBuilder::Build(const DockNode& node, const DockRect& rect, DockLayout& layout, const DockLayoutBuildSettings& settings) const {
    DockLeafLayout leaf{
        .leafId = node.id,
        .frame = rect,
        .tabStrip = DockGeometry::MakeRect(rect.x + 1, rect.y + 1, std::max(0, rect.width - 2), settings.tabStripHeight),
        .content = DockGeometry::PanelContent(rect, settings.tabStripHeight),
        .activePanelId = node.activePanelId,
    };
    layout.leaves.push_back(leaf);

    const int visibleTabs = std::max(1, static_cast<int>(node.panels.size()));
    const int maxTabWidth = std::max(1, settings.tabWidth);
    const int minTabWidth = std::clamp(settings.tabMinWidth, 1, maxTabWidth);
    const int resolvedTabWidth = std::clamp(leaf.tabStrip.width / visibleTabs, minTabWidth, maxTabWidth);
    int tabX = leaf.tabStrip.x;

    for (std::uint32_t panelId : node.panels) {
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
