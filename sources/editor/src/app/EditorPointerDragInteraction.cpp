#include "app/EditorPointerDragInteraction.hpp"

#if defined(_WIN32)
#include "app/EditorPointerDropHandler.hpp"
#include "app/EditorWindowInvalidator.hpp"
#include "app/scene_viewport/EditorSceneViewportObjectInteraction.hpp"

namespace kb::editor {
namespace {

void ApplyDragCursor() noexcept {
    SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649)));
}

[[nodiscard]] HWND ResolveDropWindow(HWND sourceWindow, HWND mainWindow, const EditorFloatingWindowManager& floatingWindows, POINT screenPoint) noexcept {
    HWND window = WindowFromPoint(screenPoint);
    const EditorFloatingWindowQueries floatingQueries = floatingWindows.Queries();
    while (window != nullptr && window != mainWindow && !floatingQueries.IsFloatingWindow(window)) {
        window = GetParent(window);
    }
    return window != nullptr ? window : sourceWindow;
}

} // namespace

void EditorPointerDragInteraction::CaptureIfActive(HWND messageWindow, const EditorPointerDragState& drag) noexcept {
    if (drag.Potential()) {
        SetCapture(messageWindow);
    }
}

bool EditorPointerDragInteraction::Move(HWND sourceWindow, HWND mainWindow, int x, int y, EditorPointerDragState& drag) noexcept {
    static_cast<void>(mainWindow);
    if (!drag.Potential()) {
        return false;
    }

    drag.dragSourceWindow = sourceWindow;
    drag.x = x;
    drag.y = y;
    if (!drag.dragging) {
        const int dx = x - drag.startX;
        const int dy = y - drag.startY;
        if ((dx * dx + dy * dy) < 36) {
            return false;
        }
        drag.dragging = true;
    }

    ApplyDragCursor();
    drag.overlayDirty = true;
    if (drag.assetCreatesMeshEntity) {
        drag.meshPreviewUpdatePending = true;
    }
    return true;
}

bool EditorPointerDragInteraction::Complete(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    EditorPointerDragState& drag) {
    if (!drag.Potential()) {
        return false;
    }

    POINT screenPoint{ x, y };
    ClientToScreen(sourceWindow, &screenPoint);
    HWND dropWindow = ResolveDropWindow(sourceWindow, mainWindow, floatingWindows, screenPoint);
    POINT dropPoint = screenPoint;
    ScreenToClient(dropWindow, &dropPoint);

    drag.x = dropPoint.x;
    drag.y = dropPoint.y;

    const bool wasActive = drag.Active();
    bool handledDrop = false;
    if (wasActive) {
        handledDrop = drag.assetCreatesMeshEntity
            && EditorSceneViewportObjectInteraction::CommitMeshDragPreview(dropWindow, mainWindow, dropPoint.x, dropPoint.y, dockModel, floatingWindows, metrics, sceneContext, drag);
        if (!handledDrop) {
            handledDrop = EditorPointerDropHandler::Drop(dropWindow, mainWindow, dropPoint.x, dropPoint.y, dockModel, floatingWindows, metrics, sceneContext, drag);
        }
    }

    EditorSceneViewportObjectInteraction::CancelMeshDragPreview(sceneContext, drag);
    drag.Clear();
    if (GetCapture() == sourceWindow) {
        ReleaseCapture();
    }
    if (wasActive) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow, sourceWindow);
    }
    return handledDrop;
}

bool EditorPointerDragInteraction::UpdateCursor(const EditorPointerDragState& drag) noexcept {
    if (!drag.Active()) {
        return false;
    }

    ApplyDragCursor();
    return true;
}

} // namespace kb::editor

#endif
