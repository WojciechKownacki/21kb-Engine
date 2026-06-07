#include "app/scene_viewport/EditorSceneViewportObjectInteraction.hpp"

#if defined(_WIN32)
#include "app/panels/EditorPanelPointerHitContext.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneComponentVisitors.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"
#include "scene/EditorViewportCameraState.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace kb::editor {
namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kGroundPlaneY = 0.0F;
constexpr float kMinRayPlaneDistance = 0.05F;
constexpr float kDefaultPickRadius = 1.0F;
constexpr float kGizmoAxisLength = 1.16F;
constexpr float kGizmoTargetPixels = 90.0F;
constexpr float kGizmoHitThresholdPixels = 10.0F;
constexpr float kGizmoCenterHitRadiusPixels = 15.0F;
constexpr float kGizmoParallelEps = 1.0e-5F;
constexpr float kGizmoMinRayDistance = 1.0e-3F;
constexpr float kMinGizmoDepth = 0.25F;

struct SceneViewportRay {
    kb::scene::Vec3 origin{};
    kb::scene::Vec3 direction{};
};

struct SceneViewportHit {
    std::uint32_t panelId = 0;
    RECT renderArea{};
    SceneViewportRay ray{};
    kb::scene::Vec3 groundPosition{};
    float localX = 0.0F;
    float localY = 0.0F;
};

struct PickContext {
    SceneViewportRay ray{};
    kb::scene::SceneEntity entity{};
    float distance = 0.0F;
    bool hit = false;
};

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] float RectWidth(const RECT& rect) noexcept {
    return static_cast<float>(std::max<LONG>(1, rect.right - rect.left));
}

[[nodiscard]] float RectHeight(const RECT& rect) noexcept {
    return static_cast<float>(std::max<LONG>(1, rect.bottom - rect.top));
}

[[nodiscard]] float DegreesToRadians(float degrees) noexcept {
    return degrees * kPi / 180.0F;
}

[[nodiscard]] kb::scene::Vec3 Add(kb::scene::Vec3 a, kb::scene::Vec3 b) noexcept {
    return kb::scene::Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] kb::scene::Vec3 Sub(kb::scene::Vec3 a, kb::scene::Vec3 b) noexcept {
    return kb::scene::Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] kb::scene::Vec3 Mul(kb::scene::Vec3 value, float scale) noexcept {
    return kb::scene::Vec3{value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] float Dot(kb::scene::Vec3 a, kb::scene::Vec3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] float LengthSquared(kb::scene::Vec3 value) noexcept {
    return Dot(value, value);
}

[[nodiscard]] kb::scene::Vec3 Normalize(kb::scene::Vec3 value) noexcept {
    const float lengthSquared = LengthSquared(value);
    if (lengthSquared <= 0.000001F) {
        return kb::scene::Vec3{};
    }
    return Mul(value, 1.0F / std::sqrt(lengthSquared));
}

[[nodiscard]] kb::scene::Vec3 AxisWorldDirection(int axis) noexcept {
    switch (axis) {
    case 0: return kb::scene::Vec3{1.0F, 0.0F, 0.0F};
    case 1: return kb::scene::Vec3{0.0F, 1.0F, 0.0F};
    case 2: return kb::scene::Vec3{0.0F, 0.0F, 1.0F};
    default: return {};
    }
}

[[nodiscard]] bool WorldToScreen(
    const EditorViewportCameraState& camera,
    const RECT& renderArea,
    kb::scene::Vec3 position,
    float& screenX,
    float& screenY) noexcept {
    const EditorViewportCameraAxes axes = camera.Axes();
    const kb::scene::Vec3 toPoint = Sub(position, axes.position);
    const float viewX = Dot(toPoint, axes.right);
    const float viewY = Dot(toPoint, axes.up);
    const float viewZ = Dot(toPoint, axes.forward);
    if (viewZ <= 0.001F) {
        return false;
    }

    const float width = RectWidth(renderArea);
    const float height = RectHeight(renderArea);
    const float aspect = width / height;
    const float tanHalfFov = std::tan(DegreesToRadians(camera.VerticalFovDegrees()) * 0.5F);
    const float ndcX = viewX / (viewZ * tanHalfFov * aspect);
    const float ndcY = viewY / (viewZ * tanHalfFov);
    screenX = (ndcX * 0.5F + 0.5F) * width;
    screenY = (1.0F - (ndcY * 0.5F + 0.5F)) * height;
    return true;
}

[[nodiscard]] float DistanceToSegment2D(float px, float py, float ax, float ay, float bx, float by) noexcept {
    const float abX = bx - ax;
    const float abY = by - ay;
    const float apX = px - ax;
    const float apY = py - ay;
    const float abLengthSquared = abX * abX + abY * abY;
    if (abLengthSquared <= 0.0001F) {
        const float dx = px - ax;
        const float dy = py - ay;
        return std::sqrt(dx * dx + dy * dy);
    }

    const float t = std::clamp((apX * abX + apY * abY) / abLengthSquared, 0.0F, 1.0F);
    const float closestX = ax + abX * t;
    const float closestY = ay + abY * t;
    const float dx = px - closestX;
    const float dy = py - closestY;
    return std::sqrt(dx * dx + dy * dy);
}

[[nodiscard]] float GizmoScreenSpaceScale(
    const EditorViewportCameraState& camera,
    const RECT& renderArea,
    kb::scene::Vec3 targetPosition) noexcept {
    const EditorViewportCameraAxes axes = camera.Axes();
    const float depth = std::max(kMinGizmoDepth, Dot(Sub(targetPosition, axes.position), axes.forward));
    const float tanHalfFov = std::tan(DegreesToRadians(camera.VerticalFovDegrees()) * 0.5F);
    const float worldPerPixel = (2.0F * depth * tanHalfFov) / RectHeight(renderArea);
    return std::clamp((kGizmoTargetPixels * worldPerPixel) / kGizmoAxisLength, 0.05F, 50000.0F);
}

[[nodiscard]] int HitTestGizmoAxis(
    const EditorViewportCameraState& camera,
    const RECT& renderArea,
    kb::scene::Vec3 targetPosition,
    float worldScale,
    float localX,
    float localY) noexcept {
    float originX = 0.0F;
    float originY = 0.0F;
    if (!WorldToScreen(camera, renderArea, targetPosition, originX, originY)) {
        return -1;
    }

    int bestAxis = -1;
    float bestDistance = kGizmoHitThresholdPixels;
    for (int axis = 0; axis < 3; ++axis) {
        const kb::scene::Vec3 end = Add(targetPosition, Mul(AxisWorldDirection(axis), worldScale * kGizmoAxisLength));
        float endX = 0.0F;
        float endY = 0.0F;
        if (!WorldToScreen(camera, renderArea, end, endX, endY)) {
            continue;
        }

        const float distance = DistanceToSegment2D(localX, localY, originX, originY, endX, endY);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestAxis = axis;
        }
    }
    return bestAxis;
}

[[nodiscard]] bool PlaneDragPosition(
    const SceneViewportRay& ray,
    kb::scene::Vec3 planePoint,
    kb::scene::Vec3 planeNormal,
    kb::scene::Vec3& hit) noexcept {
    planeNormal = Normalize(planeNormal);
    const float denom = Dot(ray.direction, planeNormal);
    if (std::abs(denom) <= kGizmoParallelEps) {
        return false;
    }

    const float distance = Dot(Sub(planePoint, ray.origin), planeNormal) / denom;
    if (distance <= kGizmoMinRayDistance) {
        return false;
    }

    hit = Add(ray.origin, Mul(ray.direction, distance));
    return true;
}

void AxisDragPlane(kb::scene::Vec3 cameraForward, int axis, kb::scene::Vec3& planeNormal, kb::scene::Vec3& removeNormal) noexcept {
    const float cx = std::abs(cameraForward.x);
    const float cy = std::abs(cameraForward.y);
    const float cz = std::abs(cameraForward.z);

    if (axis == 0) {
        planeNormal = cy > cz ? AxisWorldDirection(1) : AxisWorldDirection(2);
        removeNormal = cy > cz ? AxisWorldDirection(2) : AxisWorldDirection(1);
        return;
    }
    if (axis == 1) {
        planeNormal = cx > cz ? AxisWorldDirection(0) : AxisWorldDirection(2);
        removeNormal = cx > cz ? AxisWorldDirection(2) : AxisWorldDirection(0);
        return;
    }

    planeNormal = cx > cy ? AxisWorldDirection(0) : AxisWorldDirection(1);
    removeNormal = cx > cy ? AxisWorldDirection(1) : AxisWorldDirection(0);
}

void StoreVec3(float (&target)[3], kb::scene::Vec3 value) noexcept {
    target[0] = value.x;
    target[1] = value.y;
    target[2] = value.z;
}

[[nodiscard]] kb::scene::Vec3 LoadVec3(const float (&value)[3]) noexcept {
    return kb::scene::Vec3{value[0], value[1], value[2]};
}

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

[[nodiscard]] bool BeginAxisDrag(
    const SceneViewportHit& hit,
    const EditorViewportCameraState& camera,
    kb::scene::Vec3 targetPosition,
    int axis,
    EditorSceneGizmoAxisDrag& drag) noexcept {
    kb::scene::Vec3 planeNormal{};
    kb::scene::Vec3 removeNormal{};
    AxisDragPlane(camera.Axes().forward, axis, planeNormal, removeNormal);

    kb::scene::Vec3 startPoint{};
    if (!PlaneDragPosition(hit.ray, targetPosition, planeNormal, startPoint)) {
        return false;
    }

    StoreVec3(drag.axis, AxisWorldDirection(axis));
    StoreVec3(drag.planeNormal, planeNormal);
    StoreVec3(drag.removeNormal, removeNormal);
    StoreVec3(drag.startPoint, startPoint);
    return true;
}

[[nodiscard]] bool AxisDragDelta(
    const SceneViewportHit& hit,
    kb::scene::Vec3 targetPosition,
    const EditorSceneGizmoAxisDrag& drag,
    kb::scene::Vec3& delta) noexcept {
    kb::scene::Vec3 currentPoint{};
    if (!PlaneDragPosition(hit.ray, targetPosition, LoadVec3(drag.planeNormal), currentPoint)) {
        return false;
    }

    const kb::scene::Vec3 rawDelta = Sub(currentPoint, LoadVec3(drag.startPoint));
    const kb::scene::Vec3 removeNormal = LoadVec3(drag.removeNormal);
    const kb::scene::Vec3 planarDelta = Sub(rawDelta, Mul(removeNormal, Dot(rawDelta, removeNormal)));
    const kb::scene::Vec3 axis = LoadVec3(drag.axis);
    delta = Mul(axis, Dot(planarDelta, axis));
    return true;
}

[[nodiscard]] std::optional<kb::scene::Vec3> IntersectGroundPlane(const SceneViewportRay& ray) noexcept {
    if (std::abs(ray.direction.y) <= 0.00001F) {
        return std::nullopt;
    }

    const float distance = (kGroundPlaneY - ray.origin.y) / ray.direction.y;
    if (distance <= kMinRayPlaneDistance) {
        return std::nullopt;
    }

    return Add(ray.origin, Mul(ray.direction, distance));
}

[[nodiscard]] SceneViewportRay BuildRay(const EditorViewportCameraState& camera, const RECT& renderArea, int x, int y) noexcept {
    const EditorViewportCameraAxes axes = camera.Axes();
    const float width = RectWidth(renderArea);
    const float height = RectHeight(renderArea);
    const float normalizedX = ((static_cast<float>(x - renderArea.left) / width) * 2.0F) - 1.0F;
    const float normalizedY = 1.0F - ((static_cast<float>(y - renderArea.top) / height) * 2.0F);
    const float tanHalfFov = std::tan(DegreesToRadians(camera.VerticalFovDegrees()) * 0.5F);
    const float aspect = width / height;

    return SceneViewportRay{
        .origin = axes.position,
        .direction = Normalize(Add(axes.forward, Add(Mul(axes.right, normalizedX * aspect * tanHalfFov), Mul(axes.up, normalizedY * tanHalfFov)))),
    };
}

[[nodiscard]] std::optional<SceneViewportHit> ResolveSceneViewportRay(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    const EditorPanelPointerHitContext panelHit =
        EditorPanelPointerHitContextResolver::Resolve(sourceWindow, mainWindow, dockModel, floatingWindows, metrics, x, y);
    if (!panelHit.sceneContent.has_value()) {
        return std::nullopt;
    }

    const SceneViewportToolbarRects sceneRects = SceneViewportToolbarRenderer::Resolve(panelHit.sceneContent->content);
    if (!Contains(sceneRects.renderArea, x, y)) {
        return std::nullopt;
    }

    const SceneViewportRay ray = BuildRay(sceneContext.ViewportCamera(panelHit.sceneContent->panelId), sceneRects.renderArea, x, y);
    return SceneViewportHit{
        .panelId = panelHit.sceneContent->panelId,
        .renderArea = sceneRects.renderArea,
        .ray = ray,
        .localX = static_cast<float>(x - sceneRects.renderArea.left),
        .localY = static_cast<float>(y - sceneRects.renderArea.top),
    };
}

[[nodiscard]] std::optional<SceneViewportHit> ResolveSceneViewportHit(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    std::optional<SceneViewportHit> hit = ResolveSceneViewportRay(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    if (!hit.has_value()) {
        return std::nullopt;
    }

    const std::optional<kb::scene::Vec3> groundPosition = IntersectGroundPlane(hit->ray);
    if (!groundPosition.has_value()) {
        return std::nullopt;
    }
    hit->groundPosition = *groundPosition;
    return hit;
}

void MoveEntityTo(kb::scene::Scene& scene, kb::scene::SceneEntity entity, kb::scene::Vec3 position) {
    kb::scene::TransformComponent transform = scene.Transforms().Get(entity);
    transform.localPosition = position;
    scene.Transforms().Set(entity, transform);
}

[[nodiscard]] bool HitSphere(const SceneViewportRay& ray, kb::scene::Vec3 center, float radius, float& distance) noexcept {
    const kb::scene::Vec3 toCenter = Sub(center, ray.origin);
    const float projected = Dot(toCenter, ray.direction);
    if (projected <= 0.0F) {
        return false;
    }

    const float closestDistanceSquared = LengthSquared(Sub(toCenter, Mul(ray.direction, projected)));
    const float radiusSquared = radius * radius;
    if (closestDistanceSquared > radiusSquared) {
        return false;
    }

    distance = projected - std::sqrt(std::max(0.0F, radiusSquared - closestDistanceSquared));
    return distance > 0.0F;
}

void PickMeshVisitor(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, const kb::scene::MeshRendererComponent& renderer, void* context) {
    static_cast<void>(renderer);
    auto& pick = *static_cast<PickContext*>(context);
    float distance = 0.0F;
    if (!HitSphere(pick.ray, transform.localPosition, kDefaultPickRadius, distance)) {
        return;
    }

    if (!pick.hit || distance < pick.distance) {
        pick.entity = entity;
        pick.distance = distance;
        pick.hit = true;
    }
}

} // namespace

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
    if (!drag.assetCreatesMeshEntity || !drag.assetId.IsValid()) {
        return false;
    }

    const std::optional<SceneViewportHit> hit = ResolveSceneViewportHit(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    if (!hit.has_value()) {
        return false;
    }

    if (!drag.meshScenePreview.IsValid() || !sceneContext.Scene().Entities().IsAlive(drag.meshScenePreview)) {
        drag.meshScenePreview = sceneContext.CreateMeshAssetEntity(drag.assetId, hit->groundPosition, false);
        drag.meshScenePreviewCommitted = false;
        if (!drag.meshScenePreview.IsValid()) {
            return false;
        }
    }

    MoveEntityTo(sceneContext.Scene(), drag.meshScenePreview, hit->groundPosition);
    sceneContext.SelectEntity(drag.meshScenePreview);
    return true;
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
    if (!drag.assetCreatesMeshEntity) {
        return false;
    }

    const std::optional<SceneViewportHit> hit = ResolveSceneViewportHit(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    if (!hit.has_value()) {
        return false;
    }

    if (drag.meshScenePreview.IsValid() && sceneContext.Scene().Entities().IsAlive(drag.meshScenePreview)) {
        MoveEntityTo(sceneContext.Scene(), drag.meshScenePreview, hit->groundPosition);
        sceneContext.SelectEntity(drag.meshScenePreview);
        drag.meshScenePreviewCommitted = true;
        return true;
    }

    drag.meshScenePreview = sceneContext.CreateMeshAssetEntity(drag.assetId, hit->groundPosition, true);
    drag.meshScenePreviewCommitted = drag.meshScenePreview.IsValid();
    return drag.meshScenePreviewCommitted;
}

void EditorSceneViewportObjectInteraction::CancelMeshDragPreview(EditorSceneContext& sceneContext, EditorPointerDragState& drag) noexcept {
    if (drag.meshScenePreviewCommitted || !drag.meshScenePreview.IsValid() || !sceneContext.Scene().Entities().IsAlive(drag.meshScenePreview)) {
        return;
    }

    sceneContext.Scene().Entities().Destroy(drag.meshScenePreview);
    if (sceneContext.SelectedEntity() == drag.meshScenePreview) {
        sceneContext.ClearHierarchySelection();
    }
    drag.meshScenePreview = {};
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
    const std::optional<SceneViewportHit> hit = ResolveSceneViewportRay(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    const std::optional<kb::scene::Vec3> targetPosition = SelectedTarget(sceneContext);
    if (!hit.has_value() || !targetPosition.has_value()) {
        return false;
    }

    EditorSceneGizmoState& gizmo = sceneContext.Gizmo();
    const EditorViewportCameraState& camera = sceneContext.ViewportCamera(hit->panelId);

    float originX = 0.0F;
    float originY = 0.0F;
    if (WorldToScreen(camera, hit->renderArea, *targetPosition, originX, originY)) {
        const float dx = hit->localX - originX;
        const float dy = hit->localY - originY;
        if (std::sqrt(dx * dx + dy * dy) <= kGizmoCenterHitRadiusPixels) {
            kb::scene::Vec3 centerStart{};
            const kb::scene::Vec3 planeNormal = camera.Axes().forward;
            if (PlaneDragPosition(hit->ray, *targetPosition, planeNormal, centerStart)) {
                gizmo.draggedAxis = -1;
                gizmo.hoveredAxis = -1;
                gizmo.centerDrag = true;
                gizmo.dragStartTargetX = targetPosition->x;
                gizmo.dragStartTargetY = targetPosition->y;
                gizmo.dragStartTargetZ = targetPosition->z;
                gizmo.centerPlaneNx = planeNormal.x;
                gizmo.centerPlaneNy = planeNormal.y;
                gizmo.centerPlaneNz = planeNormal.z;
                gizmo.centerStartPx = centerStart.x;
                gizmo.centerStartPy = centerStart.y;
                gizmo.centerStartPz = centerStart.z;
                return true;
            }
        }
    }

    const float worldScale = GizmoScreenSpaceScale(camera, hit->renderArea, *targetPosition);
    const int axis = HitTestGizmoAxis(camera, hit->renderArea, *targetPosition, worldScale, hit->localX, hit->localY);
    if (axis < 0) {
        return false;
    }

    EditorSceneGizmoAxisDrag drag{};
    if (!BeginAxisDrag(*hit, camera, *targetPosition, axis, drag)) {
        return false;
    }

    gizmo.hoveredAxis = axis;
    gizmo.draggedAxis = axis;
    gizmo.centerDrag = false;
    gizmo.dragStartTargetX = targetPosition->x;
    gizmo.dragStartTargetY = targetPosition->y;
    gizmo.dragStartTargetZ = targetPosition->z;
    gizmo.axisDrag = drag;
    return true;
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
    EditorSceneGizmoState& gizmo = sceneContext.Gizmo();
    if (gizmo.IsDragging()) {
        if (!leftButtonDown) {
            return EndGizmoDrag(sceneContext);
        }

        const std::optional<SceneViewportHit> hit = ResolveSceneViewportRay(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
        if (!hit.has_value()) {
            return true;
        }

        const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();
        if (!sceneContext.Scene().Entities().IsAlive(selected)) {
            static_cast<void>(EndGizmoDrag(sceneContext));
            return true;
        }

        const kb::scene::Vec3 dragStartTarget{gizmo.dragStartTargetX, gizmo.dragStartTargetY, gizmo.dragStartTargetZ};
        if (gizmo.centerDrag) {
            kb::scene::Vec3 currentPoint{};
            const kb::scene::Vec3 planeNormal{gizmo.centerPlaneNx, gizmo.centerPlaneNy, gizmo.centerPlaneNz};
            const kb::scene::Vec3 startPoint{gizmo.centerStartPx, gizmo.centerStartPy, gizmo.centerStartPz};
            if (PlaneDragPosition(hit->ray, dragStartTarget, planeNormal, currentPoint)) {
                MoveEntityTo(sceneContext.Scene(), selected, Add(dragStartTarget, Sub(currentPoint, startPoint)));
            }
            return true;
        }

        if (gizmo.draggedAxis >= 0) {
            kb::scene::Vec3 delta{};
            if (AxisDragDelta(*hit, dragStartTarget, gizmo.axisDrag, delta)) {
                MoveEntityTo(sceneContext.Scene(), selected, Add(dragStartTarget, delta));
            }
            return true;
        }

        return true;
    }

    if (leftButtonDown) {
        return false;
    }

    const std::optional<SceneViewportHit> hit = ResolveSceneViewportRay(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    const std::optional<kb::scene::Vec3> targetPosition = SelectedTarget(sceneContext);
    int hoveredAxis = -1;
    if (hit.has_value() && targetPosition.has_value()) {
        const EditorViewportCameraState& camera = sceneContext.ViewportCamera(hit->panelId);
        const float worldScale = GizmoScreenSpaceScale(camera, hit->renderArea, *targetPosition);
        hoveredAxis = HitTestGizmoAxis(camera, hit->renderArea, *targetPosition, worldScale, hit->localX, hit->localY);
    }

    if (gizmo.hoveredAxis == hoveredAxis) {
        return false;
    }

    gizmo.hoveredAxis = hoveredAxis;
    return true;
}

bool EditorSceneViewportObjectInteraction::EndGizmoDrag(EditorSceneContext& sceneContext) noexcept {
    EditorSceneGizmoState& gizmo = sceneContext.Gizmo();
    if (!gizmo.IsDragging()) {
        return false;
    }

    gizmo.draggedAxis = -1;
    gizmo.centerDrag = false;
    return true;
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
    const std::optional<SceneViewportHit> hit = ResolveSceneViewportHit(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    if (!hit.has_value()) {
        return false;
    }

    PickContext pick{.ray = hit->ray};
    sceneContext.Scene().Components().Visitors().ForEachMeshRenderer(&PickMeshVisitor, &pick);
    if (!pick.hit) {
        sceneContext.ClearHierarchySelection();
        return true;
    }

    sceneContext.SelectEntity(pick.entity);
    return true;
}

} // namespace kb::editor

#endif
