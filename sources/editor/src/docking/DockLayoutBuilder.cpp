#include "docking/DockLayoutBuilder.hpp"

#include "docking/DockGeometry.hpp"
#include "docking/DockLayoutBuildSettings.hpp"
#include "docking/DockNodeLayoutBuilder.hpp"

#include <algorithm>

namespace kb::editor {

DockLayout DockLayoutBuilder::Build(
    const DockNode* root,
    int clientWidth,
    int clientHeight,
    int menuHeight,
    int statusBarHeight,
    int tabStripHeight,
    int tabMinWidth,
    int tabWidth,
    int splitterSize,
    std::uint32_t collapsedLeafId) const {
    const DockLayoutBuildSettings settings{
        .tabStripHeight = tabStripHeight,
        .tabMinWidth = tabMinWidth,
        .tabWidth = tabWidth,
        .splitterSize = splitterSize,
        .collapsedLeafId = collapsedLeafId,
    };

    DockLayout layout{};
    layout.menu = DockGeometry::MakeRect(0, 0, clientWidth, menuHeight);
    // One top bar: the menus on its left, the transport and Save on the rest of the same row.
    layout.toolbar = layout.menu;
    // The status bar runs along the window's bottom edge, below every docked panel.
    const int statusTop = std::max(menuHeight, clientHeight - statusBarHeight);
    layout.statusBar = DockGeometry::MakeRect(0, statusTop, clientWidth, clientHeight - statusTop);
    layout.workspace = DockGeometry::MakeRect(0, menuHeight, clientWidth, statusTop - menuHeight);

    if (root != nullptr && !layout.workspace.Empty()) {
        DockNodeLayoutBuilder{}.Build(*root, layout.workspace, layout, settings);
    }

    return layout;
}

} // namespace kb::editor
