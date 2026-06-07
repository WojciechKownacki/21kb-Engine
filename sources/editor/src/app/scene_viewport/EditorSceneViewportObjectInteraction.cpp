#include "app/scene_viewport/EditorSceneViewportObjectInteraction.hpp"

#if defined(_WIN32)
#include "app/scene_viewport/EditorSceneViewportGizmoInteraction.hpp"
#include "app/scene_viewport/EditorSceneViewportMeshDragPreview.hpp"
#include "app/scene_viewport/EditorSceneViewportSelectionInteraction.hpp"

namespace kb::editor {

bool EditorSceneViewportObjectInteraction::UpdateMeshDragPreview(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    EditorPointerDragState& drag) {
    return EditorSceneViewportMeshDragPreview::Update(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag);
}

bool EditorSceneViewportObjectInteraction::CommitMeshDragPreview(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    EditorPointerDragState& drag) {
    return EditorSceneViewportMeshDragPreview::Commit(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag);
}

void EditorSceneViewportObjectInteraction::CancelMeshDragPreview(EditorSceneContext& sceneContext, EditorPointerDragState& drag) noexcept {
    EditorSceneViewportMeshDragPreview::Cancel(sceneContext, drag);
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
    return EditorSceneViewportGizmoInteraction::UpdateDragOrHover(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, leftButtonDown);
}

bool EditorSceneViewportObjectInteraction::EndGizmoDrag(EditorSceneContext& sceneContext) noexcept {
    return EditorSceneViewportGizmoInteraction::EndDrag(sceneContext);
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

} // namespace kb::editor

#endif
