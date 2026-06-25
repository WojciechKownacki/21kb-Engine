#include "app/scene_viewport/EditorSceneViewportMeshPicker.hpp"

#if defined(_WIN32)
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponentVisitors.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace kb::editor {
namespace {

constexpr float kDefaultPickHalfExtent = 1.0F;
constexpr float kRayAabbParallelEpsilon = 0.00001F;
constexpr float kLightWirePickThresholdPixels = 8.0F;
constexpr float kLightWirePickTieEpsilon = 0.25F;
constexpr float kPi = 3.14159265358979323846F;

struct ScreenPoint {
    float x = 0.0F;
    float y = 0.0F;
};

struct LightPickCandidate {
    kb::scene::SceneEntity entity{};
    float distance = 0.0F;
    float score = 0.0F;
    float radius = 0.0F;

    [[nodiscard]] bool IsValid() const noexcept {
        return entity.IsValid();
    }
};

struct NearestPickContext {
    EditorSceneViewportRay ray{};
    EditorSceneViewportPickResult result{};
    const kb::scene::Scene* scene = nullptr;
    const EditorViewportCameraState* camera = nullptr;
    RECT renderArea{};
    ScreenPoint mouse{};
    LightPickCandidate lightPick{};
};

struct RectPickContext {
    const EditorViewportCameraState* camera = nullptr;
    RECT renderArea{};
    RECT selectionRect{};
    std::vector<kb::scene::SceneEntity> entities;
};

[[nodiscard]] kb::scene::Vec3 InverseRotate(kb::scene::Quat rotation, kb::scene::Vec3 value) noexcept {
    const kb::scene::Vec3 q{-rotation.x, -rotation.y, -rotation.z};
    const kb::scene::Vec3 uv{
        q.y * value.z - q.z * value.y,
        q.z * value.x - q.x * value.z,
        q.x * value.y - q.y * value.x,
    };
    const kb::scene::Vec3 uuv{
        q.y * uv.z - q.z * uv.y,
        q.z * uv.x - q.x * uv.z,
        q.x * uv.y - q.y * uv.x,
    };
    return EditorSceneViewportMath::Add(
        value,
        EditorSceneViewportMath::Add(
            EditorSceneViewportMath::Mul(uv, 2.0F * rotation.w),
            EditorSceneViewportMath::Mul(uuv, 2.0F)));
}

[[nodiscard]] kb::scene::Vec3 Rotate(kb::scene::Quat rotation, kb::scene::Vec3 value) noexcept {
    const kb::scene::Vec3 q{rotation.x, rotation.y, rotation.z};
    const kb::scene::Vec3 uv{
        q.y * value.z - q.z * value.y,
        q.z * value.x - q.x * value.z,
        q.x * value.y - q.y * value.x,
    };
    const kb::scene::Vec3 uuv{
        q.y * uv.z - q.z * uv.y,
        q.z * uv.x - q.x * uv.z,
        q.x * uv.y - q.y * uv.x,
    };
    return EditorSceneViewportMath::Add(
        value,
        EditorSceneViewportMath::Add(
            EditorSceneViewportMath::Mul(uv, 2.0F * rotation.w),
            EditorSceneViewportMath::Mul(uuv, 2.0F)));
}

[[nodiscard]] kb::scene::Quat Normalize(kb::scene::Quat rotation) noexcept {
    const float lengthSquared = rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w;
    if (lengthSquared <= 0.000001F) {
        return {};
    }
    const float invLength = 1.0F / std::sqrt(lengthSquared);
    return kb::scene::Quat{
        rotation.x * invLength,
        rotation.y * invLength,
        rotation.z * invLength,
        rotation.w * invLength,
    };
}

[[nodiscard]] kb::scene::Vec3 ResolveWorldPosition(const kb::scene::TransformComponent& transform) noexcept {
    const bool hasWorldPosition = transform.worldPosition.x != 0.0F || transform.worldPosition.y != 0.0F || transform.worldPosition.z != 0.0F;
    return hasWorldPosition ? transform.worldPosition : transform.localPosition;
}

[[nodiscard]] kb::scene::Quat ResolveWorldRotation(const kb::scene::TransformComponent& transform) noexcept {
    return Normalize(transform.worldRotation);
}

[[nodiscard]] float Distance(ScreenPoint lhs, ScreenPoint rhs) noexcept {
    const float dx = lhs.x - rhs.x;
    const float dy = lhs.y - rhs.y;
    return std::sqrt(dx * dx + dy * dy);
}

[[nodiscard]] float SegmentDistance(ScreenPoint point, ScreenPoint a, ScreenPoint b) noexcept {
    const float abX = b.x - a.x;
    const float abY = b.y - a.y;
    const float lengthSquared = abX * abX + abY * abY;
    if (lengthSquared <= 0.000001F) {
        return Distance(point, a);
    }
    const float t = std::clamp(((point.x - a.x) * abX + (point.y - a.y) * abY) / lengthSquared, 0.0F, 1.0F);
    return Distance(point, ScreenPoint{a.x + abX * t, a.y + abY * t});
}

[[nodiscard]] bool Project(
    const EditorViewportCameraState& camera,
    const RECT& renderArea,
    kb::scene::Vec3 position,
    ScreenPoint& output) noexcept {
    return EditorSceneViewportMath::WorldToScreen(camera, renderArea, position, output.x, output.y);
}

[[nodiscard]] bool BetterLightPick(const LightPickCandidate& candidate, const LightPickCandidate& current) noexcept {
    if (!current.IsValid()) {
        return true;
    }
    if (candidate.score + kLightWirePickTieEpsilon < current.score) {
        return true;
    }
    if (std::abs(candidate.score - current.score) <= kLightWirePickTieEpsilon) {
        if (candidate.radius + kLightWirePickTieEpsilon < current.radius) {
            return true;
        }
        return candidate.distance < current.distance;
    }
    return false;
}

void ConsiderLightPick(NearestPickContext& pick, kb::scene::SceneEntity entity, float score, float radius, float distance) {
    if (score > kLightWirePickThresholdPixels || radius <= 0.0F || distance <= 0.0F) {
        return;
    }

    LightPickCandidate candidate{
        .entity = entity,
        .distance = distance,
        .score = score,
        .radius = radius,
    };
    if (BetterLightPick(candidate, pick.lightPick)) {
        pick.lightPick = candidate;
    }
}

[[nodiscard]] bool Slab(float origin, float direction, float min, float max, float& nearDistance, float& farDistance) noexcept {
    if (std::abs(direction) <= kRayAabbParallelEpsilon) {
        return origin >= min && origin <= max;
    }

    const float invDirection = 1.0F / direction;
    float t0 = (min - origin) * invDirection;
    float t1 = (max - origin) * invDirection;
    if (t0 > t1) {
        std::swap(t0, t1);
    }

    nearDistance = std::max(nearDistance, t0);
    farDistance = std::min(farDistance, t1);
    return nearDistance <= farDistance;
}

[[nodiscard]] kb::scene::Vec3 BoxExtent(const kb::scene::TransformComponent& transform) noexcept {
    return kb::scene::Vec3{
        std::max(kDefaultPickHalfExtent, std::abs(transform.localScale.x) * kDefaultPickHalfExtent),
        std::max(kDefaultPickHalfExtent, std::abs(transform.localScale.y) * kDefaultPickHalfExtent),
        std::max(kDefaultPickHalfExtent, std::abs(transform.localScale.z) * kDefaultPickHalfExtent),
    };
}

[[nodiscard]] bool HitTransformBox(const EditorSceneViewportRay& ray, const kb::scene::TransformComponent& transform, float& distance) noexcept {
    const kb::scene::Vec3 localOrigin = InverseRotate(transform.localRotation, EditorSceneViewportMath::Sub(ray.origin, transform.localPosition));
    const kb::scene::Vec3 localDirection = InverseRotate(transform.localRotation, ray.direction);
    const kb::scene::Vec3 extent = BoxExtent(transform);

    float nearDistance = 0.0F;
    float farDistance = 1000000.0F;
    if (!Slab(localOrigin.x, localDirection.x, -extent.x, extent.x, nearDistance, farDistance) ||
        !Slab(localOrigin.y, localDirection.y, -extent.y, extent.y, nearDistance, farDistance) ||
        !Slab(localOrigin.z, localDirection.z, -extent.z, extent.z, nearDistance, farDistance)) {
        return false;
    }

    distance = nearDistance > 0.0F ? nearDistance : farDistance;
    return distance > 0.0F;
}

[[nodiscard]] RECT NormalizeRect(RECT rect) noexcept {
    if (rect.left > rect.right) {
        std::swap(rect.left, rect.right);
    }
    if (rect.top > rect.bottom) {
        std::swap(rect.top, rect.bottom);
    }
    return rect;
}

[[nodiscard]] bool ContainsPoint(const RECT& rect, float x, float y) noexcept {
    return x >= static_cast<float>(rect.left) && x <= static_cast<float>(rect.right) &&
        y >= static_cast<float>(rect.top) && y <= static_cast<float>(rect.bottom);
}

void PickNearestVisitor(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, const kb::scene::MeshRendererComponent& renderer, void* context) {
    static_cast<void>(renderer);
    auto& pick = *static_cast<NearestPickContext*>(context);
    float distance = 0.0F;
    if (!HitTransformBox(pick.ray, transform, distance)) {
        return;
    }

    if (!pick.result.IsValid() || distance < pick.result.distance) {
        pick.result.entity = entity;
        pick.result.distance = distance;
    }
}

void PickNearestLightVisitor(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, const kb::scene::LightComponent& light, void* context) {
    auto& pick = *static_cast<NearestPickContext*>(context);
    if (pick.scene == nullptr || pick.camera == nullptr || !pick.scene->Components().Visibility().Get(entity).visible) {
        return;
    }
    if (light.kind == kb::scene::LightKind::Directional || light.range <= 0.0F) {
        return;
    }

    const kb::scene::Vec3 position = ResolveWorldPosition(transform);
    const EditorViewportCameraAxes cameraAxes = pick.camera->Axes();
    const float cameraDistance = EditorSceneViewportMath::Dot(EditorSceneViewportMath::Sub(position, cameraAxes.position), cameraAxes.forward);
    if (cameraDistance <= 0.0F) {
        return;
    }

    ScreenPoint center{};
    if (!Project(*pick.camera, pick.renderArea, position, center)) {
        return;
    }

    if (light.kind == kb::scene::LightKind::Point) {
        ScreenPoint right{};
        ScreenPoint up{};
        if (!Project(*pick.camera, pick.renderArea, EditorSceneViewportMath::Add(position, EditorSceneViewportMath::Mul(cameraAxes.right, light.range)), right) ||
            !Project(*pick.camera, pick.renderArea, EditorSceneViewportMath::Add(position, EditorSceneViewportMath::Mul(cameraAxes.up, light.range)), up)) {
            return;
        }

        const float screenRadius = std::max(Distance(center, right), Distance(center, up));
        const float score = std::abs(Distance(pick.mouse, center) - screenRadius);
        ConsiderLightPick(pick, entity, score, screenRadius, cameraDistance);
        return;
    }

    const kb::scene::Quat rotation = ResolveWorldRotation(transform);
    const kb::scene::Vec3 forward = Rotate(rotation, kb::scene::Vec3{0.0F, 0.0F, 1.0F});
    const kb::scene::Vec3 rightAxis = Rotate(rotation, kb::scene::Vec3{1.0F, 0.0F, 0.0F});
    const kb::scene::Vec3 upAxis = Rotate(rotation, kb::scene::Vec3{0.0F, 1.0F, 0.0F});
    const float range = std::max(0.01F, light.range);
    const float coneRadians = std::clamp(light.outerConeDegrees, 0.0F, 179.0F) * 0.5F * kPi / 180.0F;
    const float coneRadius = std::tan(coneRadians) * range;
    const kb::scene::Vec3 endCenter = EditorSceneViewportMath::Add(position, EditorSceneViewportMath::Mul(forward, range));

    ScreenPoint ringCenter{};
    ScreenPoint ringRight{};
    ScreenPoint ringUp{};
    if (!Project(*pick.camera, pick.renderArea, endCenter, ringCenter) ||
        !Project(*pick.camera, pick.renderArea, EditorSceneViewportMath::Add(endCenter, EditorSceneViewportMath::Mul(rightAxis, coneRadius)), ringRight) ||
        !Project(*pick.camera, pick.renderArea, EditorSceneViewportMath::Add(endCenter, EditorSceneViewportMath::Mul(upAxis, coneRadius)), ringUp)) {
        return;
    }

    const float screenRadius = std::max(Distance(ringCenter, ringRight), Distance(ringCenter, ringUp));
    float score = std::abs(Distance(pick.mouse, ringCenter) - screenRadius);
    for (std::uint32_t i = 0; i < 4U; ++i) {
        const float angle = static_cast<float>(i) * kPi * 0.5F + kPi * 0.25F;
        const kb::scene::Vec3 rim = EditorSceneViewportMath::Add(
            endCenter,
            EditorSceneViewportMath::Add(
                EditorSceneViewportMath::Mul(rightAxis, std::cos(angle) * coneRadius),
                EditorSceneViewportMath::Mul(upAxis, std::sin(angle) * coneRadius)));
        ScreenPoint rimScreen{};
        if (Project(*pick.camera, pick.renderArea, rim, rimScreen)) {
            score = std::min(score, SegmentDistance(pick.mouse, center, rimScreen));
        }
    }
    ConsiderLightPick(pick, entity, score, screenRadius, cameraDistance);
}

void PickRectVisitor(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, const kb::scene::MeshRendererComponent& renderer, void* context) {
    static_cast<void>(renderer);
    auto& pick = *static_cast<RectPickContext*>(context);
    float screenX = 0.0F;
    float screenY = 0.0F;
    if (!EditorSceneViewportMath::WorldToScreen(*pick.camera, pick.renderArea, transform.localPosition, screenX, screenY)) {
        return;
    }

    if (ContainsPoint(pick.selectionRect, screenX, screenY)) {
        pick.entities.push_back(entity);
    }
}

} // namespace

EditorSceneViewportPickResult EditorSceneViewportMeshPicker::PickNearest(const kb::scene::Scene& scene, const EditorSceneViewportRay& ray) {
    NearestPickContext context{.ray = ray};
    scene.Components().Visitors().ForEachMeshRenderer(&PickNearestVisitor, &context);
    return context.result;
}

EditorSceneViewportPickResult EditorSceneViewportMeshPicker::PickNearest(
    const kb::scene::Scene& scene,
    const EditorViewportCameraState& camera,
    const RECT& renderArea,
    float screenX,
    float screenY,
    const EditorSceneViewportRay& ray) {
    NearestPickContext context{
        .ray = ray,
        .scene = &scene,
        .camera = &camera,
        .renderArea = renderArea,
        .mouse = ScreenPoint{screenX, screenY},
    };
    scene.Components().Visitors().ForEachMeshRenderer(&PickNearestVisitor, &context);
    scene.Components().Visitors().ForEachLight(&PickNearestLightVisitor, &context);
    if (context.lightPick.IsValid()) {
        return EditorSceneViewportPickResult{
            .entity = context.lightPick.entity,
            .distance = context.lightPick.distance,
        };
    }
    return context.result;
}

std::vector<kb::scene::SceneEntity> EditorSceneViewportMeshPicker::PickInsideRect(
    const kb::scene::Scene& scene,
    const EditorViewportCameraState& camera,
    const RECT& renderArea,
    const RECT& selectionRect) {
    RectPickContext context{
        .camera = &camera,
        .renderArea = renderArea,
        .selectionRect = NormalizeRect(selectionRect),
    };
    scene.Components().Visitors().ForEachMeshRenderer(&PickRectVisitor, &context);
    return context.entities;
}

} // namespace kb::editor

#endif
