#include "app/EditorMouseWheelRouter.hpp"

#if defined(_WIN32)
#include "app/console/EditorConsolePointerController.hpp"
#include "app/project_files/EditorProjectFilesMouseWheelController.hpp"
#include "app/scene_viewport/EditorSceneViewportCameraController.hpp"
#include "assets/EditorAssetBrowserGeometry.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "scene/EditorSceneContext.hpp"

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
    EditorProjectFilesMouseWheelController projectFilesWheel(sceneContext_);
    if (sceneContext_.AssetBrowser().IsDeleteConfirmOpen()) {
        RECT client{};
        GetClientRect(messageWindow_, &client);
        if (projectFilesWheel.HandleMouseWheel(client, x, y, wheelDelta)) {
            return true;
        }
        const RECT dialog = EditorAssetBrowserGeometry::DeleteConfirmRect(
            client,
            sceneContext_.AssetBrowser().DeleteConfirmOffsetX(),
            sceneContext_.AssetBrowser().DeleteConfirmOffsetY());
        if (EditorAssetBrowserGeometry::Contains(dialog, x, y)) {
            return true;
        }
    }

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
    return assetContent.has_value() && projectFilesWheel.HandleMouseWheel(*assetContent, x, y, wheelDelta);
}

} // namespace kb::editor

#endif
