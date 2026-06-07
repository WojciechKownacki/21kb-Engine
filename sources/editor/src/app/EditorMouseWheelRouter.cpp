#include "app/EditorMouseWheelRouter.hpp"

#if defined(_WIN32)
#include "app/console/EditorConsolePointerController.hpp"
#include "app/project_files/EditorProjectFilesMouseWheelController.hpp"
#include "app/scene_viewport/EditorSceneViewportCameraController.hpp"
#include "rendering/EditorPanelContentResolver.hpp"

#include <optional>

namespace kb::editor {

EditorMouseWheelRouter::EditorMouseWheelRouter(
    HWND messageWindow,
    HWND mainWindow,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& sceneViewport) noexcept
    : messageWindow_(messageWindow)
    , mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows)
    , metrics_(metrics)
    , sceneContext_(sceneContext)
    , sceneViewport_(sceneViewport) {}

bool EditorMouseWheelRouter::HandleMouseWheel(int x, int y, int wheelDelta) {
    const std::optional<RECT> consoleContent = EditorPanelContentResolver::Resolve(DockPanelKind::Console, messageWindow_, mainWindow_, dockModel_, floatingWindows_, metrics_);
    EditorConsolePointerController consolePointer(messageWindow_, sceneContext_);
    if (consoleContent.has_value() && consolePointer.HandleMouseWheel(*consoleContent, x, y, wheelDelta)) {
        return true;
    }

    EditorSceneViewportCameraController sceneCamera(mainWindow_, dockModel_, floatingWindows_, metrics_, sceneContext_, sceneViewport_);
    if (sceneCamera.HandleMouseWheel(messageWindow_, x, y, wheelDelta)) {
        return true;
    }

    const std::optional<RECT> assetContent = EditorPanelContentResolver::Resolve(DockPanelKind::Assets, messageWindow_, mainWindow_, dockModel_, floatingWindows_, metrics_);
    EditorProjectFilesMouseWheelController projectFilesWheel(sceneContext_);
    return assetContent.has_value() && projectFilesWheel.HandleMouseWheel(*assetContent, x, y, wheelDelta);
}

} // namespace kb::editor

#endif
