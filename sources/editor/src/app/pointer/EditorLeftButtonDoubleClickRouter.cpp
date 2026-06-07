#include "app/pointer/EditorLeftButtonDoubleClickRouter.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserPointerHandler.hpp"
#include "app/EditorWindowInvalidator.hpp"

namespace kb::editor {

EditorLeftButtonDoubleClickRouter::EditorLeftButtonDoubleClickRouter(
    HWND mainWindow,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    EditorSceneContext& sceneContext,
    const EditorMetrics& metrics) noexcept
    : mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows)
    , sceneContext_(sceneContext)
    , metrics_(metrics) {}

bool EditorLeftButtonDoubleClickRouter::Handle(HWND messageWindow, int x, int y) {
    if (!EditorAssetBrowserPointerHandler::HandleDoubleClick(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        return false;
    }
    sceneContext_.ClearHierarchySelection();
    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    return true;
}

} // namespace kb::editor

#endif
