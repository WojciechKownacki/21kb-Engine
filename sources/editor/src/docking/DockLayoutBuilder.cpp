#include "docking/DockLayoutBuilder.hpp"

#include "docking/DockGeometry.hpp"
#include "docking/DockLayoutBuildSettings.hpp"
#include "docking/DockNodeLayoutBuilder.hpp"

namespace kb::editor {

DockLayout DockLayoutBuilder::Build(
    const DockNode* root,
    int clientWidth,
    int clientHeight,
    int menuHeight,
    int tabStripHeight,
    int tabMinWidth,
    int tabWidth,
    int splitterSize) const {
    const DockLayoutBuildSettings settings{
        .tabStripHeight = tabStripHeight,
        .tabMinWidth = tabMinWidth,
        .tabWidth = tabWidth,
        .splitterSize = splitterSize,
    };

    DockLayout layout{};
    layout.menu = DockGeometry::MakeRect(0, 0, clientWidth, menuHeight);
    // One top bar: the menus on its left, the transport and Save on the rest of the same row.
    layout.toolbar = layout.menu;
    layout.workspace = DockGeometry::MakeRect(0, menuHeight, clientWidth, clientHeight - menuHeight);

    if (root != nullptr && !layout.workspace.Empty()) {
        DockNodeLayoutBuilder{}.Build(*root, layout.workspace, layout, settings);
    }

    return layout;
}

} // namespace kb::editor
