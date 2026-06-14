#include "app/scene_viewport/EditorSceneViewportGizmoInteraction.hpp"

#if defined(_WIN32)
#include "app/scene_viewport/EditorSceneViewportGizmoDragSolver.hpp"
#include "app/scene_viewport/EditorSceneViewportGizmoHitTester.hpp"
#include "app/scene_viewport/EditorSceneViewportHitResolver.hpp"
#include "app/scene_viewport/gizmo/EditorSceneViewportGizmoAltDuplicate.hpp"
#include "app/scene_viewport/gizmo/EditorSceneViewportGizmoDragState.hpp"
#include "app/scene_viewport/gizmo/EditorSceneViewportGizmoDragUpdater.hpp"
#include "app/scene_viewport/gizmo/EditorSceneViewportGizmoRotationDrag.hpp"
#include "app/scene_viewport/gizmo/EditorSceneViewportGizmoTargetResolver.hpp"
#include "app/scene_viewport/gizmo/EditorSceneViewportGizmoToolBehavior.hpp"
#include "engine/scene/SceneEntities.hpp"

#include <cstdint>
#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] bool CommitGizmoDragState(EditorSceneContext& sceneContext) noexcept {
    EditorSceneGizmoState& gizmo = sceneContext.Gizmo();
    if (!gizmo.IsDragging()) {
        gizmo.ClearDragPointer();
        return false;
    }

    EditorSceneViewportGizmoDragState::ClearActiveDrag(gizmo);
    static_cast<void>(sceneContext.CommitActiveTransformEdit());
    return true;
}

} // namespace

bool EditorSceneViewportGizmoInteraction::BeginDrag(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    const std::optional<EditorSceneViewportHit> hit =
        EditorSceneViewportHitResolver::ResolveRay(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    std::optional<kb::scene::Vec3> targetPosition = EditorSceneViewportGizmoTargetResolver::SelectedTarget(sceneContext);
    if (!hit.has_value() || !targetPosition.has_value()) {
        return false;
    }

    EditorSceneGizmoState& gizmo = sceneContext.Gizmo();
    const EditorViewportCameraState& camera = sceneContext.ViewportCamera(hit->panelId);
    if (gizmo.toolMode == EditorTransformToolMode::Translate &&
        EditorSceneViewportGizmoHitTester::HitCenter(camera, hit->renderArea, *targetPosition, hit->localX, hit->localY)) {
        kb::scene::Vec3 centerStart{};
        const kb::scene::Vec3 planeNormal = camera.Axes().forward;
        if (EditorSceneViewportGizmoDragSolver::PlaneDragPosition(hit->ray, *targetPosition, planeNormal, centerStart)) {
            if (!EditorSceneViewportGizmoAltDuplicate::DuplicateForTranslateDrag(sceneContext, targetPosition)) {
                return false;
            }
            if (!EditorSceneViewportGizmoDragSolver::PlaneDragPosition(hit->ray, *targetPosition, planeNormal, centerStart)) {
                return false;
            }
            if (!sceneContext.BeginSelectedTransformEdit(EditorSceneViewportGizmoToolBehavior::TransformEditLabel(gizmo.toolMode))) {
                return false;
            }
            gizmo.ClearDragPointer();
            EditorSceneViewportGizmoDragState::StartCenterDrag(gizmo, *targetPosition, planeNormal, centerStart);
            return true;
        }
    }

    const float worldScale = EditorSceneViewportGizmoHitTester::ScreenSpaceScale(camera, hit->renderArea, *targetPosition);
    const int axis = EditorSceneViewportGizmoToolBehavior::HitAxis(gizmo.toolMode, camera, hit->renderArea, *targetPosition, worldScale, hit->localX, hit->localY);
    if (axis < 0) {
        return false;
    }

    if (!EditorSceneViewportGizmoAltDuplicate::DuplicateForTranslateDrag(sceneContext, targetPosition)) {
        return false;
    }

    EditorSceneGizmoAxisDrag drag{};
    if (!EditorSceneViewportGizmoDragSolver::BeginAxisDrag(*hit, camera, *targetPosition, axis, drag)) {
        return false;
    }

    if (!sceneContext.BeginSelectedTransformEdit(EditorSceneViewportGizmoToolBehavior::TransformEditLabel(gizmo.toolMode))) {
        return false;
    }
    gizmo.ClearDragPointer();
    const float screenAngle = gizmo.toolMode == EditorTransformToolMode::Rotate
        ? EditorSceneViewportGizmoRotationDrag::ScreenAngleFromCenter(camera, hit->renderArea, *targetPosition, hit->localX, hit->localY)
        : 0.0F;
    EditorSceneViewportGizmoDragState::StartAxisDrag(gizmo, *targetPosition, axis, drag, screenAngle);
    return true;
}

bool EditorSceneViewportGizmoInteraction::UpdateDragOrHover(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    bool leftButtonDown) {
    EditorSceneGizmoState& gizmo = sceneContext.Gizmo();
    if (gizmo.IsDragging()) {
        if (!leftButtonDown) {
            return EndDrag(sceneContext);
        }
        gizmo.QueueDragPointer(reinterpret_cast<std::uintptr_t>(sourceWindow), x, y, true);
        return true;
    }

    return UpdateHover(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, leftButtonDown);
}

bool EditorSceneViewportGizmoInteraction::TickActiveDrag(
    HWND mainWindow,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    EditorSceneGizmoState& gizmo = sceneContext.Gizmo();
    if (!gizmo.IsDragging()) {
        gizmo.ClearDragPointer();
        return false;
    }

    std::uintptr_t sourceWindowId = 0U;
    int x = 0;
    int y = 0;
    bool leftButtonDown = false;
    if (!gizmo.ConsumeDragPointer(sourceWindowId, x, y, leftButtonDown)) {
        return false;
    }

    HWND sourceWindow = reinterpret_cast<HWND>(sourceWindowId);
    if (sourceWindow == nullptr || IsWindow(sourceWindow) == 0) {
        sourceWindow = mainWindow;
    }
    return UpdateActiveDrag(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, leftButtonDown);
}

bool EditorSceneViewportGizmoInteraction::EndDrag(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    if (!sceneContext.Gizmo().IsDragging()) {
        return false;
    }

    static_cast<void>(UpdateActiveDrag(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, true));
    return EndDrag(sceneContext);
}

bool EditorSceneViewportGizmoInteraction::EndDrag(EditorSceneContext& sceneContext) noexcept {
    return CommitGizmoDragState(sceneContext);
}

bool EditorSceneViewportGizmoInteraction::CancelDrag(EditorSceneContext& sceneContext) noexcept {
    EditorSceneGizmoState& gizmo = sceneContext.Gizmo();
    if (!gizmo.IsDragging()) {
        gizmo.ClearDragPointer();
        return false;
    }

    EditorSceneViewportGizmoDragState::ClearActiveDrag(gizmo);
    sceneContext.CancelActiveTransformEdit();
    return true;
}

bool EditorSceneViewportGizmoInteraction::UpdateActiveDrag(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    bool leftButtonDown) {
    if (!leftButtonDown) {
        return EndDrag(sceneContext);
    }

    const std::optional<EditorSceneViewportHit> hit =
        EditorSceneViewportHitResolver::ResolveRay(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    if (!hit.has_value()) {
        return true;
    }

    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();
    if (!sceneContext.Scene().Entities().IsAlive(selected)) {
        static_cast<void>(CancelDrag(sceneContext));
        return true;
    }

    EditorSceneGizmoState& gizmo = sceneContext.Gizmo();
    return EditorSceneViewportGizmoDragUpdater::Update(
        sceneContext,
        *hit,
        EditorSceneViewportGizmoDragState::DragStartTarget(gizmo));
}

bool EditorSceneViewportGizmoInteraction::UpdateHover(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    bool leftButtonDown) {
    if (leftButtonDown) {
        return false;
    }

    const std::optional<EditorSceneViewportHit> hit =
        EditorSceneViewportHitResolver::ResolveRay(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    const std::optional<kb::scene::Vec3> targetPosition = EditorSceneViewportGizmoTargetResolver::SelectedTarget(sceneContext);
    int hoveredAxis = -1;
    if (hit.has_value() && targetPosition.has_value()) {
        const EditorViewportCameraState& camera = sceneContext.ViewportCamera(hit->panelId);
        const float worldScale = EditorSceneViewportGizmoHitTester::ScreenSpaceScale(camera, hit->renderArea, *targetPosition);
        hoveredAxis = EditorSceneViewportGizmoToolBehavior::HitAxis(sceneContext.Gizmo().toolMode, camera, hit->renderArea, *targetPosition, worldScale, hit->localX, hit->localY);
    }

    EditorSceneGizmoState& gizmo = sceneContext.Gizmo();
    if (gizmo.hoveredAxis == hoveredAxis) {
        return false;
    }

    gizmo.hoveredAxis = hoveredAxis;
    return true;
}

} // namespace kb::editor

#endif
