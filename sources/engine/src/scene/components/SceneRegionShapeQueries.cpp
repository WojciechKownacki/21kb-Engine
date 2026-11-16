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

// The distance from the shape to a point given as its offset from the shape's centre in metres, in the entity's rotated
// frame. 2D shapes extend through the whole local Z axis, as in RegionShapeContainsLocal.
[[nodiscard]] float DistanceOutside(const RegionShapeComponent& shape, Vec3 offset, Vec3 scale) noexcept {
    const float ax = std::abs(scale.x);
    const float ay = std::abs(scale.y);
    const float az = std::abs(scale.z);
    const float mean = (ax + ay + az) / 3.0F;
    const auto excess = [](float value, float half) noexcept { return std::max(std::abs(value) - half, 0.0F); };
    switch (shape.kind) {
    case RegionShapeKind::Circle2D:
        return std::max(std::hypot(offset.x, offset.y) - shape.radius * (ax + ay) * 0.5F, 0.0F);
    case RegionShapeKind::Rectangle2D:
        return std::hypot(excess(offset.x, shape.size.x * 0.5F * ax), excess(offset.y, shape.size.y * 0.5F * ay));
    case RegionShapeKind::Sphere:
        return std::max(std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z) - shape.radius * mean, 0.0F);
    case RegionShapeKind::Box: {
        const float dx = excess(offset.x, shape.size.x * 0.5F * ax);
        const float dy = excess(offset.y, shape.size.y * 0.5F * ay);
        const float dz = excess(offset.z, shape.size.z * 0.5F * az);
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    case RegionShapeKind::Capsule: {
        const float halfLine = std::max(shape.height * 0.5F - shape.radius, 0.0F) * ay;
        const float dy = offset.y - std::clamp(offset.y, -halfLine, halfLine);
        return std::max(std::sqrt(offset.x * offset.x + dy * dy + offset.z * offset.z) - shape.radius * mean, 0.0F);
    }
    }
    return 0.0F;
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

bool SceneRegionShapeContains(const Scene& scene, SceneEntity entity, const kb::math::DVec3& worldPoint) noexcept {
    const RegionShapeComponent* shape = scene.Components().RegionShapes().TryGet(entity);
    const TransformComponent* transform = scene.Transforms().TryGet(entity);
    if (shape == nullptr || transform == nullptr || !std::isfinite(worldPoint.x) || !std::isfinite(worldPoint.y) || !std::isfinite(worldPoint.z)
        || !IsFinite(transform->worldScale)
        || std::abs(transform->worldScale.x) <= 0.000001F || std::abs(transform->worldScale.y) <= 0.000001F || std::abs(transform->worldScale.z) <= 0.000001F) return false;
    Vec3 local = kb::math::Rotate(kb::math::Inverse(transform->worldRotation),
        kb::math::RelativeTo(worldPoint, scene.Transforms().WorldTranslation(entity, *transform)));
    local.x /= transform->worldScale.x;
    local.y /= transform->worldScale.y;
    local.z /= transform->worldScale.z;
    return RegionShapeContainsLocal(*shape, local);
}

std::optional<float> SceneRegionShapeDistance(const Scene& scene, SceneEntity entity, const kb::math::DVec3& worldPoint) noexcept {
    const RegionShapeComponent* shape = scene.Components().RegionShapes().TryGet(entity);
    const TransformComponent* transform = scene.Transforms().TryGet(entity);
    if (shape == nullptr || transform == nullptr || !shape->enabled || !IsRegionShapeKindValid(shape->kind) || !IsFinite(shape->center)
        || !std::isfinite(worldPoint.x) || !std::isfinite(worldPoint.y) || !std::isfinite(worldPoint.z) || !IsFinite(transform->worldScale)
        || std::abs(transform->worldScale.x) <= 0.000001F || std::abs(transform->worldScale.y) <= 0.000001F || std::abs(transform->worldScale.z) <= 0.000001F) return std::nullopt;
    const Vec3 offset = kb::math::Rotate(kb::math::Inverse(transform->worldRotation),
        kb::math::RelativeTo(worldPoint, scene.Transforms().WorldTranslation(entity, *transform)));
    const Vec3 local{ offset.x / transform->worldScale.x, offset.y / transform->worldScale.y, offset.z / transform->worldScale.z };
    if (RegionShapeContainsLocal(*shape, local)) return 0.0F;
    const Vec3 scale = transform->worldScale;
    const Vec3 fromCenter{ offset.x - shape->center.x * scale.x, offset.y - shape->center.y * scale.y, offset.z - shape->center.z * scale.z };
    return DistanceOutside(*shape, fromCenter, scale);
}

} // namespace kb::scene
