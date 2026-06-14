#include "app/scene_viewport/gizmo/EditorSceneViewportGizmoDragUpdater.hpp"

#if defined(_WIN32)
#include "app/scene_viewport/EditorSceneViewportGizmoDragSolver.hpp"
#include "app/scene_viewport/EditorSceneViewportGizmoHitTester.hpp"
#include "app/scene_viewport/gizmo/EditorSceneViewportGizmoRotationDrag.hpp"
#include "app/scene_viewport/gizmo/EditorSceneViewportGizmoToolBehavior.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

[[nodiscard]] bool ApplyTranslateAxisDrag(
    EditorSceneContext& sceneContext,
    const EditorSceneViewportHit& hit,
    kb::scene::Vec3 dragStartTarget,
    kb::scene::Vec3 delta) {
    const EditorSceneGizmoState& gizmo = sceneContext.Gizmo();
    const kb::scene::Vec3 unsnappedPosition = EditorSceneViewportMath::Add(dragStartTarget, delta);
    static_cast<void>(sceneContext.ApplyActiveTransformEditPrimaryPosition(
        sceneContext.ViewportPreview(hit.panelId).SnapPositionAxis(unsnappedPosition, gizmo.draggedAxis)));
    return true;
}

[[nodiscard]] bool ApplyScaleAxisDrag(
    EditorSceneContext& sceneContext,
    const EditorSceneViewportHit& hit,
    kb::scene::Vec3 dragStartTarget,
    kb::scene::Vec3 delta) {
    const EditorSceneGizmoState& gizmo = sceneContext.Gizmo();
    const InspectorPropertyId property = EditorSceneViewportGizmoToolBehavior::ScalePropertyForAxis(gizmo.draggedAxis);
    const float signedDelta = EditorSceneViewportMath::Dot(delta, EditorSceneViewportMath::AxisWorldDirection(gizmo.draggedAxis));
    const float worldScale = std::max(0.001F, EditorSceneViewportGizmoHitTester::ScreenSpaceScale(
        sceneContext.ViewportCamera(hit.panelId),
        hit.renderArea,
        dragStartTarget));
    static_cast<void>(sceneContext.ApplyActiveTransformEditProperty(
        property,
        sceneContext.ActiveTransformEditPropertyStart(property) + signedDelta / worldScale));
    return true;
}

} // namespace

bool EditorSceneViewportGizmoDragUpdater::Update(
    EditorSceneContext& sceneContext,
    const EditorSceneViewportHit& hit,
    kb::scene::Vec3 dragStartTarget) {
    const EditorSceneGizmoState& gizmo = sceneContext.Gizmo();
    if (gizmo.centerDrag) {
        return UpdateCenterDrag(sceneContext, hit, dragStartTarget);
    }
    if (gizmo.draggedAxis >= 0) {
        return UpdateAxisDrag(sceneContext, hit, dragStartTarget);
    }
    return true;
}

bool EditorSceneViewportGizmoDragUpdater::UpdateCenterDrag(
    EditorSceneContext& sceneContext,
    const EditorSceneViewportHit& hit,
    kb::scene::Vec3 dragStartTarget) {
    const EditorSceneGizmoState& gizmo = sceneContext.Gizmo();
    kb::scene::Vec3 currentPoint{};
    const kb::scene::Vec3 planeNormal{gizmo.centerPlaneNx, gizmo.centerPlaneNy, gizmo.centerPlaneNz};
    const kb::scene::Vec3 startPoint{gizmo.centerStartPx, gizmo.centerStartPy, gizmo.centerStartPz};
    if (EditorSceneViewportGizmoDragSolver::PlaneDragPosition(hit.ray, dragStartTarget, planeNormal, currentPoint)) {
        const kb::scene::Vec3 unsnappedPosition =
            EditorSceneViewportMath::Add(dragStartTarget, EditorSceneViewportMath::Sub(currentPoint, startPoint));
        static_cast<void>(sceneContext.ApplyActiveTransformEditPrimaryPosition(
            sceneContext.ViewportPreview(hit.panelId).SnapPosition(unsnappedPosition)));
    }
    return true;
}

bool EditorSceneViewportGizmoDragUpdater::UpdateAxisDrag(
    EditorSceneContext& sceneContext,
    const EditorSceneViewportHit& hit,
    kb::scene::Vec3 dragStartTarget) {
    const EditorSceneGizmoState& gizmo = sceneContext.Gizmo();
    if (gizmo.toolMode == EditorTransformToolMode::Rotate) {
        return EditorSceneViewportGizmoRotationDrag::Apply(sceneContext, hit, dragStartTarget);
    }

    kb::scene::Vec3 delta{};
    if (!EditorSceneViewportGizmoDragSolver::AxisDragDelta(hit, dragStartTarget, gizmo.axisDrag, delta)) {
        return true;
    }
    if (gizmo.toolMode == EditorTransformToolMode::Translate) {
        return ApplyTranslateAxisDrag(sceneContext, hit, dragStartTarget, delta);
    }
    if (gizmo.toolMode == EditorTransformToolMode::Scale) {
        return ApplyScaleAxisDrag(sceneContext, hit, dragStartTarget, delta);
    }
    return true;
}

} // namespace kb::editor

#endif
