#include "app/scene_viewport/EditorSceneViewportObjectInteraction.hpp"

#if defined(_WIN32)
#include "app/scene_viewport/EditorSceneViewportGizmoInteraction.hpp"
#include "app/scene_viewport/EditorUIRectInteraction.hpp"
#include "app/scene_viewport/EditorSceneViewportHitResolver.hpp"
#include "app/scene_viewport/EditorSceneViewportAssetDragPreview.hpp"
#include "app/scene_viewport/EditorSceneViewportSelectionInteraction.hpp"

namespace kb::editor {

bool EditorSceneViewportObjectInteraction::UpdateScenePlacementPreview(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    EditorPointerDragState& drag) {
    return EditorSceneViewportAssetDragPreview::Update(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag);
}

bool EditorSceneViewportObjectInteraction::CommitScenePlacementPreview(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    EditorPointerDragState& drag) {
    return EditorSceneViewportAssetDragPreview::Commit(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag);
}

void EditorSceneViewportObjectInteraction::CancelScenePlacementPreview(EditorSceneContext& sceneContext, EditorPointerDragState& drag) noexcept {
    EditorSceneViewportAssetDragPreview::Cancel(sceneContext, drag);
}

bool EditorSceneViewportObjectInteraction::BeginGizmoDrag(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    const auto hit = EditorSceneViewportHitResolver::ResolveRay(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    if (hit && sceneContext.ViewportPreview(hit->panelId).Is2D() && !sceneContext.HasPlayModeSceneSession()) {
        const auto& preview = sceneContext.ViewportPreview(hit->panelId);
        const float zoom = preview.UIZoom();
        const auto pan = preview.UIPan();
        const bool handled = EditorUIRectInteraction::Begin(sceneContext,
            static_cast<float>(hit->renderArea.right-hit->renderArea.left), static_cast<float>(hit->renderArea.bottom-hit->renderArea.top), (hit->localX-pan.x)/zoom, (hit->localY-pan.y)/zoom, zoom);
        if (sceneContext.UIRectDrag()) {
            sceneContext.UIRectDrag()->viewportOrigin = {static_cast<float>(hit->renderArea.left)+pan.x,static_cast<float>(hit->renderArea.top)+pan.y};
            sceneContext.UIRectDrag()->pointerScale = 1.0F / zoom;
        }
        return handled;
    }
    return EditorSceneViewportGizmoInteraction::BeginDrag(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
}

bool EditorSceneViewportObjectInteraction::UpdateGizmoDragOrHover(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    bool leftButtonDown) {
    if (sceneContext.UIRectDrag()) {
        if (!leftButtonDown) return EditorUIRectInteraction::End(sceneContext);
        const auto origin = sceneContext.UIRectDrag()->viewportOrigin;
        return EditorUIRectInteraction::Update(sceneContext, (static_cast<float>(x)-origin.x)*sceneContext.UIRectDrag()->pointerScale, (static_cast<float>(y)-origin.y)*sceneContext.UIRectDrag()->pointerScale,
            (GetKeyState(VK_SHIFT)&0x8000)!=0, (GetKeyState(VK_MENU)&0x8000)!=0);
    }
    return EditorSceneViewportGizmoInteraction::UpdateDragOrHover(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, leftButtonDown);
}

bool EditorSceneViewportObjectInteraction::TickGizmoDrag(
    HWND mainWindow,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    return EditorSceneViewportGizmoInteraction::TickActiveDrag(mainWindow, dockModel, floatingWindows, metrics, sceneContext);
}

bool EditorSceneViewportObjectInteraction::EndGizmoDrag(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    if (sceneContext.UIRectDrag()) {
        const auto origin = sceneContext.UIRectDrag()->viewportOrigin;
        static_cast<void>(EditorUIRectInteraction::Update(sceneContext, (static_cast<float>(x)-origin.x)*sceneContext.UIRectDrag()->pointerScale, (static_cast<float>(y)-origin.y)*sceneContext.UIRectDrag()->pointerScale,
            (GetKeyState(VK_SHIFT)&0x8000)!=0, (GetKeyState(VK_MENU)&0x8000)!=0));
        return EditorUIRectInteraction::End(sceneContext);
    }
    return EditorSceneViewportGizmoInteraction::EndDrag(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
}

bool EditorSceneViewportObjectInteraction::EndGizmoDrag(EditorSceneContext& sceneContext) noexcept {
    if (EditorUIRectInteraction::End(sceneContext)) return true;
    return EditorSceneViewportGizmoInteraction::EndDrag(sceneContext);
}

bool EditorSceneViewportObjectInteraction::CancelGizmoDrag(EditorSceneContext& sceneContext) noexcept {
    if (EditorUIRectInteraction::End(sceneContext, true)) return true;
    return EditorSceneViewportGizmoInteraction::CancelDrag(sceneContext);
}

bool EditorSceneViewportObjectInteraction::SelectAt(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    return EditorSceneViewportSelectionInteraction::SelectAt(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
}

bool EditorSceneViewportObjectInteraction::BeginBoxSelection(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    return EditorSceneViewportSelectionInteraction::BeginBoxSelection(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
}

bool EditorSceneViewportObjectInteraction::UpdateBoxSelection(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    bool leftButtonDown) {
    return EditorSceneViewportSelectionInteraction::UpdateBoxSelection(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, leftButtonDown);
}

bool EditorSceneViewportObjectInteraction::CommitBoxSelection(EditorSceneContext& sceneContext) {
    return EditorSceneViewportSelectionInteraction::CommitBoxSelection(sceneContext);
}

} // namespace kb::editor

#endif
