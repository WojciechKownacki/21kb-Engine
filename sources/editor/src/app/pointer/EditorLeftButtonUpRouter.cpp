#include "app/pointer/EditorLeftButtonUpRouter.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserPointerHandler.hpp"
#include "app/EditorPointerDragInteraction.hpp"
#include "app/EditorWindowInvalidator.hpp"
#include "app/console/EditorConsolePointerController.hpp"
#include "app/inspector/EditorInspectorPointerController.hpp"
#include "app/scene_viewport/EditorSceneViewportObjectInteraction.hpp"

namespace kb::editor {

EditorLeftButtonUpRouter::EditorLeftButtonUpRouter(
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

void EditorLeftButtonUpRouter::Handle(HWND messageWindow, int x, int y) {
    static_cast<void>(x);
    static_cast<void>(y);
    shellInteraction_.ClearPressedSave();
    shellInteraction_.ClearPressedTransport();
    if (sceneContext_.IsHierarchyScrollbarDragging()) {
        sceneContext_.EndHierarchyScrollbarDrag();
        ReleaseCapture();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    EditorInspectorPointerController inspectorPointer(sceneContext_);
    const bool wasDraggingMeshPreview = sceneContext_.Inspector().IsDraggingMeshPreview();
    if (inspectorPointer.HandlePointerUp()) {
        ReleaseCapture();
        if (!wasDraggingMeshPreview) {
            sceneViewport_.RequestPresent();
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (EditorSceneViewportObjectInteraction::EndGizmoDrag(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        ReleaseCapture();
        sceneViewport_.RequestPresent();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (pointerDrag_.Potential()) {
        const bool handledDrop = EditorPointerDragInteraction::Complete(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_, pointerDrag_);
        if (handledDrop) {
            sceneViewport_.RequestPresent();
        }
        return;
    }

    if (EditorAssetBrowserPointerHandler::HandlePointerUp(sceneContext_)) {
        ReleaseCapture();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }
    EditorConsolePointerController consolePointer(messageWindow, sceneContext_);
    if (consolePointer.HandlePointerUp()) {
        ReleaseCapture();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (dockController_.HandlePointerUp(messageWindow)) {
        sceneViewport_.RequestPresent();
    }
}

} // namespace kb::editor

#endif
