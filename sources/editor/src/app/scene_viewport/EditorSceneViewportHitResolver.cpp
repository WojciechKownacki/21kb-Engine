#include "app/scene_viewport/EditorSceneViewportHitResolver.hpp"

#if defined(_WIN32)
#include "app/panels/EditorPanelPointerHitContext.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"

#include <cmath>
#include <optional>

namespace kb::editor {
namespace {

constexpr float kGroundPlaneY = 0.0F;
constexpr float kMinRayPlaneDistance = 0.05F;

[[nodiscard]] EditorSceneViewportRay BuildRay(const EditorViewportCameraState& camera, const RECT& renderArea, int x, int y) noexcept {
    const EditorViewportCameraAxes axes = camera.Axes();
    const float width = EditorSceneViewportMath::RectWidth(renderArea);
    const float height = EditorSceneViewportMath::RectHeight(renderArea);
    const float normalizedX = ((static_cast<float>(x - renderArea.left) / width) * 2.0F) - 1.0F;
    const float normalizedY = 1.0F - ((static_cast<float>(y - renderArea.top) / height) * 2.0F);
    const float tanHalfFov = std::tan(EditorSceneViewportMath::DegreesToRadians(camera.VerticalFovDegrees()) * 0.5F);
    const float aspect = width / height;

    return EditorSceneViewportRay{
        .origin = axes.position,
        .direction = EditorSceneViewportMath::Normalize(EditorSceneViewportMath::Add(
            axes.forward,
            EditorSceneViewportMath::Add(
                EditorSceneViewportMath::Mul(axes.right, normalizedX * aspect * tanHalfFov),
                EditorSceneViewportMath::Mul(axes.up, normalizedY * tanHalfFov)))),
    };
}

[[nodiscard]] std::optional<kb::scene::Vec3> IntersectGroundPlane(const EditorSceneViewportRay& ray) noexcept {
    if (std::abs(ray.direction.y) <= 0.00001F) {
        return std::nullopt;
    }

    const float distance = (kGroundPlaneY - ray.origin.y) / ray.direction.y;
    if (distance <= kMinRayPlaneDistance) {
        return std::nullopt;
    }

    return EditorSceneViewportMath::Add(ray.origin, EditorSceneViewportMath::Mul(ray.direction, distance));
}

} // namespace

std::optional<EditorSceneViewportHit> EditorSceneViewportHitResolver::ResolveRay(
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

    const SceneViewportToolbarRects sceneRects = SceneViewportToolbarRenderer::Resolve(
        panelHit.sceneContent->content,
        sceneContext.ViewportPreview(panelHit.sceneContent->panelId));
    if (!EditorSceneViewportMath::Contains(sceneRects.renderArea, x, y)) {
        return std::nullopt;
    }

    const EditorSceneViewportRay ray = BuildRay(sceneContext.ViewportCamera(panelHit.sceneContent->panelId), sceneRects.renderArea, x, y);
    return EditorSceneViewportHit{
        .panelId = panelHit.sceneContent->panelId,
        .renderArea = sceneRects.renderArea,
        .ray = ray,
        .localX = static_cast<float>(x - sceneRects.renderArea.left),
        .localY = static_cast<float>(y - sceneRects.renderArea.top),
    };
}

std::optional<EditorSceneViewportHit> EditorSceneViewportHitResolver::ResolveGroundHit(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    std::optional<EditorSceneViewportHit> hit = ResolveRay(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
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

} // namespace kb::editor

#endif
