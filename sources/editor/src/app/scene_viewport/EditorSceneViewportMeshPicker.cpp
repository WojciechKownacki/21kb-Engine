#include "app/scene_viewport/EditorSceneViewportMeshPicker.hpp"

#if defined(_WIN32)
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

struct NearestPickContext {
    EditorSceneViewportRay ray{};
    EditorSceneViewportPickResult result{};
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
