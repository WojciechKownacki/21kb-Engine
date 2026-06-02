#include "app/EditorWindowPointerHandler.hpp"

#if defined(_WIN32)
#include "app/EditorPointerDragInteraction.hpp"
#include "app/EditorPointerDragSourceResolver.hpp"
#include "app/EditorAssetBrowserPointerHandler.hpp"
#include "app/EditorWindowInvalidator.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"
#include "scene/EditorHierarchyContentResolver.hpp"

#include <windowsx.h>

#include <optional>

namespace kb::editor {
namespace {

bool CommitPendingNewAssetFolder(EditorSceneContext& sceneContext) {
    if (sceneContext.AssetBrowser().TextEditMode() != EditorAssetTextEditMode::NewFolder) {
        return false;
    }
    static_cast<void>(sceneContext.CommitAssetTextEdit());
    return true;
}

bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

} // namespace

EditorWindowPointerHandler::EditorWindowPointerHandler(
    HWND mainWindow,
    EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows,
    EditorDockController& dockController,
    EditorHierarchySelectionController& hierarchySelection,
    EditorSceneContext& sceneContext,
    EditorPointerDragState& pointerDrag,
    const EditorMetrics& metrics) noexcept
    : mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows)
    , dockController_(dockController)
    , hierarchySelection_(hierarchySelection)
    , sceneContext_(sceneContext)
    , pointerDrag_(pointerDrag)
    , metrics_(metrics) {}

LRESULT EditorWindowPointerHandler::HandleLeftButtonDown(HWND messageWindow, LPARAM lparam) {
    const int x = GET_X_LPARAM(lparam);
    const int y = GET_Y_LPARAM(lparam);
    const bool committedNewFolder = CommitPendingNewAssetFolder(sceneContext_);
    if (committedNewFolder) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }
    const std::optional<RECT> assetContent = EditorPanelContentResolver::Resolve(DockPanelKind::Assets, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    const std::optional<RECT> hierarchyContent = EditorHierarchyContentResolver::Resolve(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    const std::optional<EditorResolvedPanelContent> sceneContent = EditorPanelContentResolver::ResolvePanel(DockPanelKind::Scene, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    const bool inAssetPanel = assetContent.has_value() && PointInRect(*assetContent, x, y);
    const bool inHierarchyPanel = hierarchyContent.has_value() && PointInRect(*hierarchyContent, x, y);

    if (sceneContent.has_value()) {
        const SceneViewportToolbarRects sceneToolbar = SceneViewportToolbarRenderer::Resolve(sceneContent->content);
        if (PointInRect(sceneToolbar.profileButton, x, y)) {
            sceneContext_.ViewportPreview(sceneContent->panelId).CycleProfile();
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return 0;
        }
        if (PointInRect(sceneToolbar.fitButton, x, y)) {
            sceneContext_.ViewportPreview(sceneContent->panelId).CycleFitMode();
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return 0;
        }
        if (PointInRect(sceneToolbar.cameraButton, x, y)) {
            sceneContext_.ViewportPreview(sceneContent->panelId).CycleCameraMode();
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return 0;
        }
    }

    EditorPointerDragSourceResolver::Resolve(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_, pointerDrag_);
    EditorPointerDragInteraction::CaptureIfActive(messageWindow, pointerDrag_);

    if (hierarchySelection_.HandlePointerDown(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        sceneContext_.AssetBrowser().FocusSelection(false);
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }

    if (EditorAssetBrowserPointerHandler::HandlePointerDown(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        sceneContext_.ClearHierarchySelection();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }

    if (!inAssetPanel) {
        sceneContext_.AssetBrowser().FocusSelection(false);
    }
    if (!inHierarchyPanel) {
        sceneContext_.ClearHierarchySelection();
    }
    dockController_.HandlePointerDown(messageWindow, x, y);
    return 0;
}

LRESULT EditorWindowPointerHandler::HandleRightButtonDown(HWND messageWindow, LPARAM lparam) {
    const int x = GET_X_LPARAM(lparam);
    const int y = GET_Y_LPARAM(lparam);
    const bool committedNewFolder = CommitPendingNewAssetFolder(sceneContext_);
    if (committedNewFolder) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }

    pointerDrag_.Clear();
    if (EditorAssetBrowserPointerHandler::HandleRightButtonDown(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }

    return 0;
}

LRESULT EditorWindowPointerHandler::HandleLeftButtonDoubleClick(HWND messageWindow, LPARAM lparam) {
    const int x = GET_X_LPARAM(lparam);
    const int y = GET_Y_LPARAM(lparam);

    if (EditorAssetBrowserPointerHandler::HandleDoubleClick(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        sceneContext_.ClearHierarchySelection();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }

    return HandleLeftButtonDown(messageWindow, lparam);
}

LRESULT EditorWindowPointerHandler::HandleMouseMove(HWND messageWindow, LPARAM lparam) {
    if (EditorAssetBrowserPointerHandler::HandlePointerMove(messageWindow, mainWindow_, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }
    if (EditorPointerDragInteraction::Move(messageWindow, mainWindow_, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), pointerDrag_)) {
        return 0;
    }
    dockController_.HandlePointerMove(messageWindow, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
    return 0;
}

LRESULT EditorWindowPointerHandler::HandleLeftButtonUp(HWND messageWindow, LPARAM lparam) {
    const int x = GET_X_LPARAM(lparam);
    const int y = GET_Y_LPARAM(lparam);

    if (EditorAssetBrowserPointerHandler::HandlePointerUp(sceneContext_)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }

    const bool handledDrop = EditorPointerDragInteraction::Complete(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_, pointerDrag_);
    if (handledDrop) {
        return 0;
    }

    dockController_.HandlePointerUp(messageWindow);
    return 0;
}

LRESULT EditorWindowPointerHandler::HandleSetCursor(HWND messageWindow, WPARAM wparam, LPARAM lparam) {
    if (LOWORD(lparam) != HTCLIENT) {
        return DefWindowProcW(messageWindow, WM_SETCURSOR, wparam, lparam);
    }

    POINT point{};
    GetCursorPos(&point);
    ScreenToClient(messageWindow, &point);
    if (EditorPointerDragInteraction::UpdateCursor(pointerDrag_)) {
        return TRUE;
    }
    dockController_.UpdateHoverCursor(messageWindow, point.x, point.y);
    return TRUE;
}

} // namespace kb::editor

#endif
