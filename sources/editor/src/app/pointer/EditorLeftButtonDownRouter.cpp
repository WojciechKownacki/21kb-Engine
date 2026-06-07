#include "app/pointer/EditorLeftButtonDownRouter.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserPointerHandler.hpp"
#include "app/EditorPendingTextEditCommitter.hpp"
#include "app/EditorPointerDragInteraction.hpp"
#include "app/EditorPointerDragSourceResolver.hpp"
#include "app/EditorWindowInvalidator.hpp"
#include "app/EditorWindowToolbarPointerHandler.hpp"
#include "app/console/EditorConsolePointerController.hpp"
#include "app/docking/EditorMainDockSplitterPointerController.hpp"
#include "app/inspector/EditorInspectorPointerController.hpp"
#include "app/panels/EditorPanelPointerHitContext.hpp"
#include "app/project_files/EditorProjectFilesDeleteConfirmOverlayController.hpp"
#include "app/project_files/EditorProjectFilesTransientUiController.hpp"
#include "app/scene_viewport/EditorSceneViewportToolbarPointerController.hpp"

namespace kb::editor {

EditorLeftButtonDownRouter::EditorLeftButtonDownRouter(
    HWND mainWindow,
    EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows,
    EditorDockController& dockController,
    EditorHierarchySelectionController& hierarchySelection,
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& sceneViewport,
    EditorPlayModeState& playMode,
    EditorShellInteractionState& shellInteraction,
    EditorPointerDragState& pointerDrag,
    const EditorMetrics& metrics) noexcept
    : mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows)
    , dockController_(dockController)
    , hierarchySelection_(hierarchySelection)
    , sceneContext_(sceneContext)
    , sceneViewport_(sceneViewport)
    , playMode_(playMode)
    , shellInteraction_(shellInteraction)
    , pointerDrag_(pointerDrag)
    , metrics_(metrics) {}

void EditorLeftButtonDownRouter::Handle(HWND messageWindow, int x, int y) {
    const EditorProjectFilesDeleteConfirmOverlayController deleteConfirm(messageWindow, sceneContext_);
    if (deleteConfirm.HandlePointerDown(x, y)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    EditorPendingTextEditCommitter pendingTextEdits(sceneContext_);
    if (pendingTextEdits.CommitPendingEdits()) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }
    if (EditorWindowToolbarPointerHandler::HandleLeftButtonDown(mainWindow_, messageWindow, x, y, dockModel_, playMode_, shellInteraction_, metrics_)) {
        return;
    }
    const EditorPanelPointerHitContext panelHit =
        EditorPanelPointerHitContextResolver::Resolve(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_, x, y);

    if (panelHit.sceneContent.has_value()) {
        EditorSceneViewportToolbarPointerController sceneToolbar(sceneContext_, sceneViewport_);
        if (sceneToolbar.HandlePointerDown(*panelHit.sceneContent, x, y)) {
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
    }

    EditorMainDockSplitterPointerController mainSplitter(mainWindow_, dockModel_, dockController_, sceneViewport_, metrics_);
    if (mainSplitter.HandlePointerDown(messageWindow, x, y)) {
        return;
    }

    EditorPointerDragSourceResolver::Resolve(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_, pointerDrag_);
    EditorPointerDragInteraction::CaptureIfActive(messageWindow, pointerDrag_);

    if (hierarchySelection_.HandlePointerDown(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        sceneContext_.AssetBrowser().FocusSelection(false);
        EditorProjectFilesTransientUiController(sceneContext_).CloseTransientUi();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (EditorAssetBrowserPointerHandler::HandlePointerDown(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        sceneContext_.ClearHierarchySelection();
        if (EditorAssetBrowserPointerHandler::RequiresMouseCapture(sceneContext_)) {
            SetCapture(messageWindow);
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    EditorConsolePointerController consolePointer(messageWindow, sceneContext_);
    if (panelHit.inConsolePanel && consolePointer.HandlePointerDown(*panelHit.consoleContent, x, y)) {
        sceneContext_.ClearHierarchySelection();
        sceneContext_.AssetBrowser().FocusSelection(false);
        EditorProjectFilesTransientUiController(sceneContext_).CloseTransientUi();
        if (sceneContext_.Console().IsDetailResizeDragging() || sceneContext_.Console().IsDetailScrollbarDragging() || sceneContext_.Console().IsListScrollbarDragging()) {
            SetCapture(messageWindow);
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (panelHit.inInspectorPanel) {
        EditorInspectorPointerController inspectorPointer(sceneContext_);
        static_cast<void>(inspectorPointer.HandlePointerDown(*panelHit.inspectorContent, x, y));
        if (inspectorPointer.ShouldCaptureMouse()) {
            SetCapture(messageWindow);
        }
        sceneViewport_.RequestPresent();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (!panelHit.inAssetPanel) {
        sceneContext_.AssetBrowser().FocusSelection(false);
        EditorProjectFilesTransientUiController(sceneContext_).CloseTransientUi();
    }
    if (!panelHit.inHierarchyPanel && !panelHit.inConsolePanel) {
        sceneContext_.ClearHierarchySelection();
    }
    if (dockController_.HandlePointerDown(messageWindow, x, y)) {
        sceneViewport_.RequestPresent();
    }
}

} // namespace kb::editor

#endif
