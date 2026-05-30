#include "docking/DockMainLayoutResolver.hpp"

#if defined(_WIN32)

namespace kb::editor {

DockLayout DockMainLayoutResolver::Resolve(HWND mainWindow, const EditorDockModel& dockModel, const EditorMetrics& metrics) {
    RECT client{};
    GetClientRect(mainWindow, &client);
    return dockModel.Queries().BuildLayout(
        client.right - client.left,
        client.bottom - client.top,
        metrics.menuHeight,
        metrics.toolbarHeight,
        metrics.tabStripHeight,
        metrics.tabMinWidth,
        metrics.tabWidth,
        metrics.splitterSize,
        metrics.panelPadding);
}

} // namespace kb::editor

#endif
