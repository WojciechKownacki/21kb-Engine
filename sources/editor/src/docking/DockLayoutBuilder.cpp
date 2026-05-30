#include "docking/DockLayoutBuilder.hpp"

#include "docking/DockGeometry.hpp"

#include <algorithm>
#include <cmath>

namespace kb::editor {

DockLayout DockLayoutBuilder::Build(
    const DockNode* root,
    int clientWidth,
    int clientHeight,
    int menuHeight,
    int toolbarHeight,
    int tabStripHeight,
    int tabMinWidth,
    int tabWidth,
    int splitterSize,
    int panelPadding) const {
    DockLayout layout{};
    layout.menu = DockGeometry::MakeRect(0, 0, clientWidth, menuHeight);
    layout.toolbar = DockGeometry::MakeRect(0, menuHeight, clientWidth, toolbarHeight);
    layout.workspace = DockGeometry::MakeRect(0, menuHeight + toolbarHeight + splitterSize, clientWidth, clientHeight - menuHeight - toolbarHeight - splitterSize);

    if (root != nullptr && !layout.workspace.Empty()) {
        BuildNodeLayout(*root, layout.workspace, layout, tabStripHeight, tabMinWidth, tabWidth, splitterSize, panelPadding);
    }
    return layout;
}

void DockLayoutBuilder::BuildNodeLayout(const DockNode& node, const DockRect& rect, DockLayout& layout, int tabStripHeight, int tabMinWidth, int tabWidth, int splitterSize, int panelPadding) const {
    static_cast<void>(panelPadding);

    if (node.kind == DockNode::Kind::Leaf) {
        DockLeafLayout leaf{
            .leafId = node.id,
            .frame = rect,
            .tabStrip = DockGeometry::MakeRect(rect.x + 1, rect.y + 1, std::max(0, rect.width - 2), tabStripHeight),
            .content = DockGeometry::MakeRect(rect.x + 1, rect.y + tabStripHeight + 1, std::max(0, rect.width - 2), std::max(0, rect.height - tabStripHeight - 2)),
            .activePanelId = node.activePanelId,
        };
        layout.leaves.push_back(leaf);

        const int visibleTabs = std::max(1, static_cast<int>(node.panels.size()));
        const int maxTabWidth = std::max(1, tabWidth);
        const int minTabWidth = std::clamp(tabMinWidth, 1, maxTabWidth);
        const int resolvedTabWidth = std::clamp(leaf.tabStrip.width / visibleTabs, minTabWidth, maxTabWidth);
        int tabX = leaf.tabStrip.x;
        for (std::uint32_t panelId : node.panels) {
            layout.panels.push_back(DockPanelLayout{
                .panelId = panelId,
                .leafId = node.id,
                .tab = DockGeometry::MakeRect(tabX, leaf.tabStrip.y, std::min(resolvedTabWidth, leaf.tabStrip.x + leaf.tabStrip.width - tabX), tabStripHeight),
                .content = leaf.content,
                .active = panelId == node.activePanelId,
            });
            tabX += resolvedTabWidth;
        }
        return;
    }

    const int splitter = splitterSize;
    if (node.axis == DockSplitAxis::Horizontal) {
        const int firstWidth = DockGeometry::ClampInt(static_cast<int>(std::round(static_cast<float>(rect.width - splitter) * node.ratio)), 80, std::max(80, rect.width - splitter - 80));
        DockRect firstRect = DockGeometry::MakeRect(rect.x, rect.y, firstWidth, rect.height);
        DockRect splitterRect = DockGeometry::MakeRect(rect.x + firstWidth, rect.y, splitter, rect.height);
        DockRect secondRect = DockGeometry::MakeRect(splitterRect.x + splitter, rect.y, rect.width - firstWidth - splitter, rect.height);
        if (node.first != nullptr) {
            BuildNodeLayout(*node.first, firstRect, layout, tabStripHeight, tabMinWidth, tabWidth, splitterSize, panelPadding);
        }
        layout.splitters.push_back(DockSplitterLayout{ .nodeId = node.id, .axis = node.axis, .rect = splitterRect, .container = rect });
        if (node.second != nullptr) {
            BuildNodeLayout(*node.second, secondRect, layout, tabStripHeight, tabMinWidth, tabWidth, splitterSize, panelPadding);
        }
    } else {
        const int firstHeight = DockGeometry::ClampInt(static_cast<int>(std::round(static_cast<float>(rect.height - splitter) * node.ratio)), 80, std::max(80, rect.height - splitter - 80));
        DockRect firstRect = DockGeometry::MakeRect(rect.x, rect.y, rect.width, firstHeight);
        DockRect splitterRect = DockGeometry::MakeRect(rect.x, rect.y + firstHeight, rect.width, splitter);
        DockRect secondRect = DockGeometry::MakeRect(rect.x, splitterRect.y + splitter, rect.width, rect.height - firstHeight - splitter);
        if (node.first != nullptr) {
            BuildNodeLayout(*node.first, firstRect, layout, tabStripHeight, tabMinWidth, tabWidth, splitterSize, panelPadding);
        }
        layout.splitters.push_back(DockSplitterLayout{ .nodeId = node.id, .axis = node.axis, .rect = splitterRect, .container = rect });
        if (node.second != nullptr) {
            BuildNodeLayout(*node.second, secondRect, layout, tabStripHeight, tabMinWidth, tabWidth, splitterSize, panelPadding);
        }
    }
}

} // namespace kb::editor
