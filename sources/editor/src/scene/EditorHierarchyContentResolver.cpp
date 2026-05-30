#include "scene/EditorHierarchyContentResolver.hpp"

#if defined(_WIN32)
#include "scene/EditorHierarchyFloatingContentResolver.hpp"
#include "scene/EditorHierarchyMainContentResolver.hpp"

namespace kb::editor {

std::optional<RECT> EditorHierarchyContentResolver::Resolve(
    HWND sourceWindow,
    HWND mainWindow,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics) {
    if (sourceWindow == nullptr || mainWindow == nullptr) {
        return std::nullopt;
    }

    if (sourceWindow == mainWindow) {
        return EditorHierarchyMainContentResolver::Resolve(mainWindow, dockModel, metrics);
    }

    return EditorHierarchyFloatingContentResolver::Resolve(sourceWindow, dockModel, floatingWindows, metrics);
}

} // namespace kb::editor

#endif
