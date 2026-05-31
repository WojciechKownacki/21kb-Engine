#include "app/EditorDropPanelResolver.hpp"

#if defined(_WIN32)
#include "rendering/EditorPanelContentResolver.hpp"

namespace kb::editor {

std::optional<RECT> EditorDropPanelResolver::Resolve(DockPanelKind kind, HWND sourceWindow, HWND mainWindow, const EditorDockModel& dockModel, const EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics) {
    return EditorPanelContentResolver::Resolve(kind, sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
}

} // namespace kb::editor

#endif
