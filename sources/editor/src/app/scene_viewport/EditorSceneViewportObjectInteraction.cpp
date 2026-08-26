#include "app/scene_viewport/EditorSceneViewportObjectInteraction.hpp"

#if defined(_WIN32)
#include "app/scene_viewport/EditorSceneViewportGizmoInteraction.hpp"
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
    return EditorSceneViewportGizmoInteraction::EndDrag(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
}

bool EditorSceneViewportObjectInteraction::EndGizmoDrag(EditorSceneContext& sceneContext) noexcept {
    return EditorSceneViewportGizmoInteraction::EndDrag(sceneContext);
}

bool EditorSceneViewportObjectInteraction::CancelGizmoDrag(EditorSceneContext& sceneContext) noexcept {
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
