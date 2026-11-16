#include "docking/DockWindowLayoutResolver.hpp"

#if defined(_WIN32)
#include "docking/DockMainLayoutResolver.hpp"
#include "windowing/FloatingWindowControlLayout.hpp"

namespace kb::editor {

DockLayout DockWindowLayoutResolver::Resolve(HWND window, HWND mainWindow, const EditorDockModel& dockModel, const EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics) {
    if (window == mainWindow) {
        return DockMainLayoutResolver::Resolve(mainWindow, dockModel, metrics);
    }
    RECT client{};
    GetClientRect(window, &client);
    return ResolveFloating(client, floatingWindows.Queries().GroupId(window), dockModel, metrics);
}

DockLayout DockWindowLayoutResolver::ResolveFloating(const RECT& client, std::uint32_t groupId, const EditorDockModel& dockModel, const EditorMetrics& metrics) {
    return dockModel.Queries().BuildFloatingLayout(
        groupId,
        client.right - client.left,
        client.bottom - client.top,
        metrics.tabStripHeight, metrics.tabMinWidth, metrics.tabWidth,
        FloatingWindowControlLayout::TotalWidth(metrics));
}

} // namespace kb::editor

#endif
