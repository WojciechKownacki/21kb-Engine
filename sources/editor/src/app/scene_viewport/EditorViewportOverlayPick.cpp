#include "app/scene_viewport/EditorViewportOverlayPick.hpp"

#if defined(_WIN32)
#include <algorithm>
#include <cmath>

namespace kb::editor {
namespace {

// Half the side of the square searched around the cursor, before display scaling.
constexpr float kWirePickTolerancePixels = 5.0F;
// Screen distances closer than this are the same line for the eye; the nearer one wins.
constexpr float kScreenTiePixels = 0.5F;
constexpr float kMinLineDepth = 0.01F;

[[nodiscard]] float ViewDepth(const EditorViewportCameraAxes& axes, kb::scene::Vec3 point) noexcept {
    return EditorSceneViewportMath::Dot(EditorSceneViewportMath::Sub(point, axes.position), axes.forward);
}

[[nodiscard]] kb::scene::Vec3 Lerp(kb::scene::Vec3 a, kb::scene::Vec3 b, float t) noexcept {
    return EditorSceneViewportMath::Add(a, EditorSceneViewportMath::Mul(EditorSceneViewportMath::Sub(b, a), t));
}

[[nodiscard]] float CameraDistance(const EditorViewportCameraAxes& axes, kb::scene::Vec3 point) noexcept {
    return std::sqrt(EditorSceneViewportMath::LengthSquared(EditorSceneViewportMath::Sub(point, axes.position)));
}

} // namespace

float EditorViewportScreenSegmentDistance(
    float pointX, float pointY, float ax, float ay, float bx, float by, float& along) noexcept {
    const float abX = bx - ax;
    const float abY = by - ay;
    const float lengthSquared = abX * abX + abY * abY;
    along = lengthSquared <= 0.000001F
        ? 0.0F
        : std::clamp(((pointX - ax) * abX + (pointY - ay) * abY) / lengthSquared, 0.0F, 1.0F);
    const float dx = pointX - (ax + abX * along);
    const float dy = pointY - (ay + abY * along);
    return std::sqrt(dx * dx + dy * dy);
}

float EditorViewportWirePickTolerancePixels(float dpiScale) noexcept {
    return kWirePickTolerancePixels * std::max(1.0F, dpiScale);
}

bool EditorViewportOverlayHitIsBetter(
    const EditorViewportOverlayHit& candidate, const EditorViewportOverlayHit& current) noexcept {
    if (!candidate.IsValid()) {
        return false;
    }
    if (!current.IsValid()) {
        return true;
    }
    if (std::abs(candidate.pixelDistance - current.pixelDistance) > kScreenTiePixels) {
        return candidate.pixelDistance < current.pixelDistance;
    }
    return candidate.depth < current.depth;
}

EditorSceneViewportPickResult ResolveViewportClickPick(
    const EditorSceneViewportPickResult& geometry, const EditorViewportOverlayHit& overlay) noexcept {
    if (!overlay.IsValid() || (geometry.IsValid() && geometry.distance < overlay.depth)) {
        return geometry;
    }
    return EditorSceneViewportPickResult{.entity = overlay.entity, .distance = overlay.depth};
}

EditorViewportOverlayPicker::EditorViewportOverlayPicker(
    const EditorViewportCameraState& camera, const RECT& renderArea, float clickX, float clickY, float tolerancePixels) noexcept
    : camera_(&camera),
      axes_(camera.Axes()),
      renderArea_(renderArea),
      clickX_(clickX),
      clickY_(clickY),
      tolerancePixels_(tolerancePixels) {}

void EditorViewportOverlayPicker::ConsiderLine(kb::scene::SceneEntity entity, kb::scene::Vec3 from, kb::scene::Vec3 to) noexcept {
    // Keep only the part of the line in front of the near plane; the rest does not reach the screen.
    const float nearDepth = std::max(kMinLineDepth, camera_->NearClip());
    const float fromDepth = ViewDepth(axes_, from);
    const float toDepth = ViewDepth(axes_, to);
    if (fromDepth < nearDepth && toDepth < nearDepth) {
        return;
    }
    if (fromDepth < nearDepth) {
        from = Lerp(from, to, (nearDepth - fromDepth) / (toDepth - fromDepth));
    } else if (toDepth < nearDepth) {
        to = Lerp(to, from, (nearDepth - toDepth) / (fromDepth - toDepth));
    }

    float fromX = 0.0F;
    float fromY = 0.0F;
    float toX = 0.0F;
    float toY = 0.0F;
    if (!EditorSceneViewportMath::WorldToScreen(*camera_, renderArea_, from, fromX, fromY) ||
        !EditorSceneViewportMath::WorldToScreen(*camera_, renderArea_, to, toX, toY)) {
        return;
    }
    float along = 0.0F;
    const float pixels = EditorViewportScreenSegmentDistance(clickX_, clickY_, fromX, fromY, toX, toY, along);
    if (pixels > tolerancePixels_) {
        return;
    }
    Offer(EditorViewportOverlayHit{
        .entity = entity,
        .pixelDistance = pixels,
        .depth = CameraDistance(axes_, Lerp(from, to, along)),
    });
}

void EditorViewportOverlayPicker::ConsiderIcon(kb::scene::SceneEntity entity, kb::scene::Vec3 position, float halfSizePixels) noexcept {
    float centerX = 0.0F;
    float centerY = 0.0F;
    if (ViewDepth(axes_, position) < kMinLineDepth ||
        !EditorSceneViewportMath::WorldToScreen(*camera_, renderArea_, position, centerX, centerY)) {
        return;
    }
    // Inside the icon's square is a direct hit; just outside it counts like a line nearby.
    const float outsideX = std::max(0.0F, std::abs(clickX_ - centerX) - halfSizePixels);
    const float outsideY = std::max(0.0F, std::abs(clickY_ - centerY) - halfSizePixels);
    const float pixels = std::sqrt(outsideX * outsideX + outsideY * outsideY);
    if (pixels > tolerancePixels_) {
        return;
    }
    Offer(EditorViewportOverlayHit{
        .entity = entity,
        .pixelDistance = pixels,
        .depth = CameraDistance(axes_, position),
    });
}

void EditorViewportOverlayPicker::Offer(const EditorViewportOverlayHit& candidate) noexcept {
    if (EditorViewportOverlayHitIsBetter(candidate, best_)) {
        best_ = candidate;
    }
}

} // namespace kb::editor

#endif
