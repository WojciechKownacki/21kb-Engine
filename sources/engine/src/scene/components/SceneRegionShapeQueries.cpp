#include "engine/scene/SceneRegionShapeQueries.hpp"

#include "engine/math/EngineMath.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <algorithm>
#include <cmath>

namespace kb::scene {
namespace {

[[nodiscard]] bool IsFinite(Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool IsFinitePositive(float value) noexcept { return std::isfinite(value) && value > 0.0F; }

[[nodiscard]] bool WithinCircle(float x, float y, float radius) noexcept {
    return (x * x) + (y * y) <= radius * radius;
}

} // namespace

bool RegionShapeContainsLocal(const RegionShapeComponent& shape, Vec3 localPoint) noexcept {
    if (!shape.enabled || !IsRegionShapeKindValid(shape.kind) || !IsFinite(localPoint) || !IsFinite(shape.center)) {
        return false;
    }
    const Vec3 point{ localPoint.x - shape.center.x, localPoint.y - shape.center.y, localPoint.z - shape.center.z };
    switch (shape.kind) {
    case RegionShapeKind::Circle2D:
        return IsFinitePositive(shape.radius) && WithinCircle(point.x, point.y, shape.radius);
    case RegionShapeKind::Rectangle2D:
        return IsFinitePositive(shape.size.x) && IsFinitePositive(shape.size.y)
            && std::abs(point.x) <= shape.size.x * 0.5F && std::abs(point.y) <= shape.size.y * 0.5F;
    case RegionShapeKind::Sphere:
        return IsFinitePositive(shape.radius) && WithinCircle(point.x, point.y, shape.radius)
            && (point.z * point.z) <= shape.radius * shape.radius - (point.x * point.x) - (point.y * point.y);
    case RegionShapeKind::Box:
        return IsFinitePositive(shape.size.x) && IsFinitePositive(shape.size.y) && IsFinitePositive(shape.size.z)
            && std::abs(point.x) <= shape.size.x * 0.5F && std::abs(point.y) <= shape.size.y * 0.5F && std::abs(point.z) <= shape.size.z * 0.5F;
    case RegionShapeKind::Capsule: {
        if (!IsFinitePositive(shape.radius) || !IsFinitePositive(shape.height) || shape.height < 2.0F * shape.radius) return false;
        const float halfLine = (shape.height * 0.5F) - shape.radius;
        const float nearestY = std::clamp(point.y, -halfLine, halfLine);
        const float dy = point.y - nearestY;
        return (point.x * point.x) + (dy * dy) + (point.z * point.z) <= shape.radius * shape.radius;
    }
    }
    return false;
}

bool SceneRegionShapeContains(const Scene& scene, SceneEntity entity, Vec3 worldPoint) noexcept {
    const RegionShapeComponent* shape = scene.Components().RegionShapes().TryGet(entity);
    const TransformComponent* transform = scene.Transforms().TryGet(entity);
    if (shape == nullptr || transform == nullptr || !IsFinite(worldPoint) || !IsFinite(transform->worldScale)
        || std::abs(transform->worldScale.x) <= 0.000001F || std::abs(transform->worldScale.y) <= 0.000001F || std::abs(transform->worldScale.z) <= 0.000001F) return false;
    Vec3 local = kb::math::Rotate(kb::math::Inverse(transform->worldRotation), worldPoint - transform->worldPosition);
    local.x /= transform->worldScale.x;
    local.y /= transform->worldScale.y;
    local.z /= transform->worldScale.z;
    return RegionShapeContainsLocal(*shape, local);
}

} // namespace kb::scene
