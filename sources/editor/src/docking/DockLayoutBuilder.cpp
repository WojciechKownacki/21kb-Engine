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
    int toolbarHeight,
    int tabStripHeight,
    int tabMinWidth,
    int tabWidth,
    int splitterSize,
    int panelPadding) const {
    const DockLayoutBuildSettings settings{
        .tabStripHeight = tabStripHeight,
        .tabMinWidth = tabMinWidth,
        .tabWidth = tabWidth,
        .splitterSize = splitterSize,
        .panelPadding = panelPadding,
    };

    DockLayout layout{};
    layout.menu = DockGeometry::MakeRect(0, 0, clientWidth, menuHeight);
    layout.toolbar = DockGeometry::MakeRect(0, menuHeight, clientWidth, toolbarHeight);
    layout.workspace = DockGeometry::MakeRect(0, menuHeight + toolbarHeight, clientWidth, clientHeight - menuHeight - toolbarHeight);

    if (root != nullptr && !layout.workspace.Empty()) {
        DockNodeLayoutBuilder{}.Build(*root, layout.workspace, layout, settings);
    }

    return layout;
}

} // namespace kb::editor
