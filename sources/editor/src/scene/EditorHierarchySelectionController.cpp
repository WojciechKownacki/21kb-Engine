#include "scene/EditorHierarchySelectionController.hpp"

#if defined(_WIN32)
#include "scene/EditorHierarchyContentResolver.hpp"
#include "scene/EditorHierarchyRowPicker.hpp"

namespace kb::editor {

bool EditorHierarchySelectionController::HandlePointerDown(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) const {
    const std::optional<RECT> content = EditorHierarchyContentResolver::Resolve(sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (!content.has_value()) {
        return false;
    }

    const bool additive = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool range = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool handled = EditorHierarchyRowPicker::SelectAtContentPoint(*content, x, y, additive, range, sceneContext);
    if (handled) {
        SetFocus(sourceWindow);
    }
    return handled;
}

} // namespace kb::editor

#endif
