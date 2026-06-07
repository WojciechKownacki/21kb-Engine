#include "app/scene_viewport/EditorSceneViewportGizmoInteraction.hpp"

#if defined(_WIN32)
#include "app/scene_viewport/EditorSceneViewportGizmoDragSolver.hpp"
#include "app/scene_viewport/EditorSceneViewportGizmoHitTester.hpp"
#include "app/scene_viewport/EditorSceneViewportHitResolver.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] std::optional<kb::scene::Vec3> SelectedTarget(EditorSceneContext& sceneContext) noexcept {
    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();
    if (!sceneContext.Scene().Entities().IsAlive(selected)) {
        return std::nullopt;
    }

    const kb::scene::TransformComponent* transform = sceneContext.Scene().Transforms().TryGet(selected);
    if (transform == nullptr) {
        return std::nullopt;
    }
    return transform->localPosition;
}

void StartCenterDrag(
    EditorSceneGizmoState& gizmo,
    kb::scene::Vec3 targetPosition,
    kb::scene::Vec3 planeNormal,
    kb::scene::Vec3 startPoint) noexcept {
    gizmo.draggedAxis = -1;
    gizmo.hoveredAxis = -1;
    gizmo.centerDrag = true;
    gizmo.dragStartTargetX = targetPosition.x;
    gizmo.dragStartTargetY = targetPosition.y;
    gizmo.dragStartTargetZ = targetPosition.z;
    gizmo.centerPlaneNx = planeNormal.x;
    gizmo.centerPlaneNy = planeNormal.y;
    gizmo.centerPlaneNz = planeNormal.z;
    gizmo.centerStartPx = startPoint.x;
    gizmo.centerStartPy = startPoint.y;
    gizmo.centerStartPz = startPoint.z;
}

void StartAxisDrag(
    EditorSceneGizmoState& gizmo,
    kb::scene::Vec3 targetPosition,
    int axis,
    const EditorSceneGizmoAxisDrag& drag) noexcept {
    gizmo.hoveredAxis = axis;
    gizmo.draggedAxis = axis;
    gizmo.centerDrag = false;
    gizmo.dragStartTargetX = targetPosition.x;
    gizmo.dragStartTargetY = targetPosition.y;
    gizmo.dragStartTargetZ = targetPosition.z;
    gizmo.axisDrag = drag;
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
    const std::optional<kb::scene::Vec3> targetPosition = SelectedTarget(sceneContext);
    if (!hit.has_value() || !targetPosition.has_value()) {
        return false;
    }

    EditorSceneGizmoState& gizmo = sceneContext.Gizmo();
    const EditorViewportCameraState& camera = sceneContext.ViewportCamera(hit->panelId);
    if (EditorSceneViewportGizmoHitTester::HitCenter(camera, hit->renderArea, *targetPosition, hit->localX, hit->localY)) {
        kb::scene::Vec3 centerStart{};
        const kb::scene::Vec3 planeNormal = camera.Axes().forward;
        if (EditorSceneViewportGizmoDragSolver::PlaneDragPosition(hit->ray, *targetPosition, planeNormal, centerStart)) {
            if (!sceneContext.BeginSceneEditTransaction("Move Entity")) {
                return false;
            }
            StartCenterDrag(gizmo, *targetPosition, planeNormal, centerStart);
            return true;
        }
    }

    const float worldScale = EditorSceneViewportGizmoHitTester::ScreenSpaceScale(camera, hit->renderArea, *targetPosition);
    const int axis = EditorSceneViewportGizmoHitTester::HitAxis(camera, hit->renderArea, *targetPosition, worldScale, hit->localX, hit->localY);
    if (axis < 0) {
        return false;
    }

    EditorSceneGizmoAxisDrag drag{};
    if (!EditorSceneViewportGizmoDragSolver::BeginAxisDrag(*hit, camera, *targetPosition, axis, drag)) {
        return false;
    }

    if (!sceneContext.BeginSceneEditTransaction("Move Entity")) {
        return false;
    }
    StartAxisDrag(gizmo, *targetPosition, axis, drag);
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
        return UpdateActiveDrag(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, leftButtonDown);
    }

    return UpdateHover(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, leftButtonDown);
}

bool EditorSceneViewportGizmoInteraction::EndDrag(EditorSceneContext& sceneContext) noexcept {
    EditorSceneGizmoState& gizmo = sceneContext.Gizmo();
    if (!gizmo.IsDragging()) {
        return false;
    }

    gizmo.draggedAxis = -1;
    gizmo.centerDrag = false;
    static_cast<void>(sceneContext.CommitSceneEditTransaction());
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
        sceneContext.CancelSceneEditTransaction();
        static_cast<void>(EndDrag(sceneContext));
        return true;
    }

    EditorSceneGizmoState& gizmo = sceneContext.Gizmo();
    const kb::scene::Vec3 dragStartTarget{gizmo.dragStartTargetX, gizmo.dragStartTargetY, gizmo.dragStartTargetZ};
    if (gizmo.centerDrag) {
        kb::scene::Vec3 currentPoint{};
        const kb::scene::Vec3 planeNormal{gizmo.centerPlaneNx, gizmo.centerPlaneNy, gizmo.centerPlaneNz};
        const kb::scene::Vec3 startPoint{gizmo.centerStartPx, gizmo.centerStartPy, gizmo.centerStartPz};
        if (EditorSceneViewportGizmoDragSolver::PlaneDragPosition(hit->ray, dragStartTarget, planeNormal, currentPoint)) {
            EditorSceneViewportMath::MoveEntityTo(
                sceneContext.Scene(),
                selected,
                EditorSceneViewportMath::Add(dragStartTarget, EditorSceneViewportMath::Sub(currentPoint, startPoint)));
        }
        return true;
    }

    if (gizmo.draggedAxis >= 0) {
        kb::scene::Vec3 delta{};
        if (EditorSceneViewportGizmoDragSolver::AxisDragDelta(*hit, dragStartTarget, gizmo.axisDrag, delta)) {
            EditorSceneViewportMath::MoveEntityTo(sceneContext.Scene(), selected, EditorSceneViewportMath::Add(dragStartTarget, delta));
        }
        return true;
    }

    return true;
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
    const std::optional<kb::scene::Vec3> targetPosition = SelectedTarget(sceneContext);
    int hoveredAxis = -1;
    if (hit.has_value() && targetPosition.has_value()) {
        const EditorViewportCameraState& camera = sceneContext.ViewportCamera(hit->panelId);
        const float worldScale = EditorSceneViewportGizmoHitTester::ScreenSpaceScale(camera, hit->renderArea, *targetPosition);
        hoveredAxis = EditorSceneViewportGizmoHitTester::HitAxis(camera, hit->renderArea, *targetPosition, worldScale, hit->localX, hit->localY);
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
