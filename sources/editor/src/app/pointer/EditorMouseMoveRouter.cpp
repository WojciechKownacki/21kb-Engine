#include "app/pointer/EditorMouseMoveRouter.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserPointerHandler.hpp"
#include "app/EditorPointerDragInteraction.hpp"
#include "app/EditorWindowInvalidator.hpp"
#include "app/EditorWindowToolbarPointerHandler.hpp"
#include "app/console/EditorConsolePointerController.hpp"
#include "app/cursor/EditorInternalSplitterCursorController.hpp"
#include "app/inspector/EditorInspectorPointerController.hpp"
#include "app/scene_viewport/EditorSceneViewportCameraController.hpp"
#include "rendering/EditorPanelContentResolver.hpp"

#include <optional>

namespace kb::editor {

EditorMouseMoveRouter::EditorMouseMoveRouter(
    HWND mainWindow,
    EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows,
    EditorDockController& dockController,
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& sceneViewport,
    EditorShellInteractionState& shellInteraction,
    EditorPointerDragState& pointerDrag,
    const EditorMetrics& metrics) noexcept
    : mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows)
    , dockController_(dockController)
    , sceneContext_(sceneContext)
    , sceneViewport_(sceneViewport)
    , shellInteraction_(shellInteraction)
    , pointerDrag_(pointerDrag)
    , metrics_(metrics) {}

void EditorMouseMoveRouter::Handle(HWND messageWindow, int x, int y, bool leftButtonDown) {
    EditorSceneViewportCameraController sceneCamera(mainWindow_, dockModel_, floatingWindows_, metrics_, sceneContext_, sceneViewport_);
    if (sceneCamera.HandlePointerMove(messageWindow, x, y)) {
        return;
    }

    const EditorInternalSplitterCursorController splitterCursor(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_, sceneContext_);
    splitterCursor.UpdateCursor(x, y);
    static_cast<void>(EditorWindowToolbarPointerHandler::HandleMouseMove(mainWindow_, messageWindow, x, y, dockModel_, shellInteraction_, metrics_));
    EditorInspectorPointerController inspectorPointer(sceneContext_);

    if (leftButtonDown && inspectorPointer.HandlePointerDrag(x, y)) {
        sceneViewport_.RequestPresent();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (leftButtonDown && pointerDrag_.Potential() && EditorPointerDragInteraction::Move(messageWindow, mainWindow_, x, y, pointerDrag_)) {
        sceneViewport_.RequestPresent();
        return;
    }

    if (leftButtonDown && dockController_.HandlePointerMove(messageWindow, x, y, true)) {
        sceneViewport_.RequestPresent();
        return;
    }

    const std::optional<RECT> consoleContent = EditorPanelContentResolver::Resolve(DockPanelKind::Console, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    EditorConsolePointerController consolePointer(messageWindow, sceneContext_);
    if (consolePointer.UpdateHoverOrClear(consoleContent, x, y)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }
    if (consolePointer.HandlePointerMove(consoleContent, y, leftButtonDown)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (EditorAssetBrowserPointerHandler::HandlePointerMove(messageWindow, mainWindow_, x, y, leftButtonDown, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }
    const std::optional<RECT> inspectorContent = EditorPanelContentResolver::Resolve(DockPanelKind::Inspector, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    if (inspectorPointer.UpdateHoverOrClear(inspectorContent, x, y)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }
    if (inspectorPointer.Contains(inspectorContent, x, y)) {
        return;
    }
    if (dockController_.HandlePointerMove(messageWindow, x, y, leftButtonDown)) {
        sceneViewport_.RequestPresent();
    }
    splitterCursor.UpdateCursor(x, y);
}

} // namespace kb::editor

#endif
