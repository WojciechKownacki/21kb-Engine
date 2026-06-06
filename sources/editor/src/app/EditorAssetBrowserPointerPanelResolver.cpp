#include "app/EditorAssetBrowserPointerPanelResolver.hpp"

#if defined(_WIN32)
#include "rendering/EditorPanelContentResolver.hpp"

namespace kb::editor {

std::optional<RECT> EditorAssetBrowserPointerPanelResolver::ResolveContent(
    HWND sourceWindow,
    HWND mainWindow,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics) {
    return EditorPanelContentResolver::Resolve(DockPanelKind::Assets, sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
}

} // namespace kb::editor

#endif
