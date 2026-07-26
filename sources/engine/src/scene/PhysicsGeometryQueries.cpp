#include "scene/PhysicsGeometryQueries.hpp"

#include "engine/math/EngineMath.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kb::scene {
namespace {

using kb::math::Abs;
using kb::math::Dot;
using kb::math::Max;
using kb::math::Normalize;
using kb::math::Inverse;
using kb::math::Rotate;

[[nodiscard]] bool IntersectRaySphere(Vec3 origin, Vec3 direction, float maxDistance, Vec3 center, float radius, float& outDistance, Vec3& outNormal) noexcept {
    const Vec3 oc = origin - center;
    const float b = Dot(oc, direction);
    const float c = Dot(oc, oc) - radius * radius;
    const float discriminant = b * b - c;
    if (discriminant < 0.0F) {
        return false;
    }
    const float root = std::sqrt(discriminant);
    float candidate = -b - root;
    if (candidate < 0.0F) {
        candidate = -b + root;
    }
    if (candidate < 0.0F || candidate > maxDistance) {
        return false;
    }
    outDistance = candidate;
    outNormal = Normalize((origin + direction * outDistance) - center);
    return true;
}

[[nodiscard]] bool IntersectRayAabb(Vec3 origin, Vec3 direction, float maxDistance, Vec3 center, Vec3 halfExtents, float& outDistance, Vec3& outNormal) noexcept {
    const Vec3 minimum = center - halfExtents;
    const Vec3 maximum = center + halfExtents;
    float tMin = 0.0F;
    float tMax = maxDistance;
    Vec3 hitNormal{};

    const auto testAxis = [&](float rayOrigin, float rayDirection, float minValue, float maxValue, Vec3 axisNormal) {
        if (std::abs(rayDirection) <= 0.000001F) {
            return rayOrigin >= minValue && rayOrigin <= maxValue;
        }
        float t1 = (minValue - rayOrigin) / rayDirection;
        float t2 = (maxValue - rayOrigin) / rayDirection;
        Vec3 normal1 = axisNormal * -1.0F;
        Vec3 normal2 = axisNormal;
        if (t1 > t2) {
            std::swap(t1, t2);
            std::swap(normal1, normal2);
        }
        if (t1 > tMin) {
            tMin = t1;
            hitNormal = normal1;
        }
        tMax = std::min(tMax, t2);
        return tMin <= tMax;
    };

    if (!testAxis(origin.x, direction.x, minimum.x, maximum.x, Vec3{ 1.0F, 0.0F, 0.0F }) ||
        !testAxis(origin.y, direction.y, minimum.y, maximum.y, Vec3{ 0.0F, 1.0F, 0.0F }) ||
        !testAxis(origin.z, direction.z, minimum.z, maximum.z, Vec3{ 0.0F, 0.0F, 1.0F })) {
        return false;
    }

    outDistance = tMin;
    outNormal = hitNormal;
    return outDistance >= 0.0F && outDistance <= maxDistance;
}

// The capsule's cylindrical middle needs its own intersection: testing only
// the two cap spheres misses a ray through the middle of any non-degenerate
// capsule. The ray direction is normalized by the caller, so `t` remains a
// world-space distance after the rotation into collider-local space.
[[nodiscard]] bool IntersectRayCapsule(Vec3 origin, Vec3 direction, float maxDistance, float radius, float halfCylinder, float& outDistance, Vec3& outNormal) noexcept {
    float bestDistance = std::numeric_limits<float>::max();
    Vec3 bestNormal{};
    bool hit = false;

    const float radialDirectionSquared = direction.x * direction.x + direction.z * direction.z;
    if (radialDirectionSquared > 0.000001F) {
        const float radialOriginDirection = origin.x * direction.x + origin.z * direction.z;
        const float radialOriginSquared = origin.x * origin.x + origin.z * origin.z;
        const float discriminant = radialOriginDirection * radialOriginDirection - radialDirectionSquared * (radialOriginSquared - radius * radius);
        if (discriminant >= 0.0F) {
            const float root = std::sqrt(discriminant);
            for (const float candidate : { (-radialOriginDirection - root) / radialDirectionSquared, (-radialOriginDirection + root) / radialDirectionSquared }) {
                const float y = origin.y + direction.y * candidate;
                if (candidate >= 0.0F && candidate <= maxDistance && y >= -halfCylinder && y <= halfCylinder && candidate < bestDistance) {
                    bestDistance = candidate;
                    bestNormal = Normalize(Vec3{ origin.x + direction.x * candidate, 0.0F, origin.z + direction.z * candidate });
                    hit = true;
                }
            }
        }
    }

    for (const Vec3 capCenter : { Vec3{ 0.0F, halfCylinder, 0.0F }, Vec3{ 0.0F, -halfCylinder, 0.0F } }) {
        float candidateDistance = 0.0F;
        Vec3 candidateNormal{};
        if (IntersectRaySphere(origin, direction, maxDistance, capCenter, radius, candidateDistance, candidateNormal) && candidateDistance < bestDistance) {
            bestDistance = candidateDistance;
            bestNormal = candidateNormal;
            hit = true;
        }
    }

    if (!hit) {
        return false;
    }
    outDistance = bestDistance;
    outNormal = bestNormal;
    return true;
}

struct RaycastAllVisitorContext {
    Scene* scene = nullptr;
    Vec3 origin{};
    Vec3 direction{};
    float maxDistance = 0.0F;
    std::uint32_t layerMask = kPhysicsAllLayers;
    kb::library::ArrayNonAlloc<PhysicsCastResult>* results = nullptr;
};

[[nodiscard]] bool CastResultLess(const PhysicsCastResult& lhs, const PhysicsCastResult& rhs) noexcept {
    return lhs.distance < rhs.distance ||
        (lhs.distance == rhs.distance && lhs.entity.Id() < rhs.entity.Id());
}

void InsertBoundedCastResult(
    kb::library::ArrayNonAlloc<PhysicsCastResult>& results,
    PhysicsCastResult candidate) {
    const std::size_t count = results.Count();
    const std::size_t capacity = results.Capacity();
    if (capacity == 0U) {
        return;
    }

    std::size_t insertAt = 0U;
    while (insertAt < count && !CastResultLess(candidate, *results.GetAt(insertAt))) {
        ++insertAt;
    }
    if (insertAt >= capacity) {
        return;
    }

    if (count < capacity) {
        [[maybe_unused]] const bool appended = results.PushBack(candidate);
        for (std::size_t index = count; index > insertAt; --index) {
            [[maybe_unused]] const bool shifted = results.SetAt(index, *results.GetAt(index - 1U));
        }
    } else {
        for (std::size_t index = count - 1U; index > insertAt; --index) {
            [[maybe_unused]] const bool shifted = results.SetAt(index, *results.GetAt(index - 1U));
        }
    }
    [[maybe_unused]] const bool inserted = results.SetAt(insertAt, candidate);
}

void RaycastAllVisitor(SceneEntity entity, const TransformComponent& transform, void* rawContext) {
    auto* context = static_cast<RaycastAllVisitorContext*>(rawContext);
    if (context == nullptr || context->scene == nullptr || context->results == nullptr) {
        return;
    }
    const ColliderComponent* collider = context->scene->Components().Colliders().TryGet(entity);
    if (collider == nullptr || (collider->layer & context->layerMask) == 0U) {
        return;
    }
    float distance = 0.0F;
    Vec3 normal{};
    if (!IntersectRayCollider(context->origin, context->direction, context->maxDistance, *collider, transform, distance, normal)) {
        return;
    }
    // Maintain the closest Capacity() hits while visiting. Transform
    // iteration order is not a distance order, so merely ignoring PushBack
    // failures would retain whichever bodies happened to be visited first.
    InsertBoundedCastResult(*context->results, PhysicsCastResult{
        .hit = true,
        .entity = entity,
        .distance = distance,
        .point = context->origin + context->direction * distance,
        .normal = normal,
    });
}

} // namespace

bool IntersectRayCollider(
    Vec3 origin,
    Vec3 direction,
    float maxDistance,
    const ColliderComponent& collider,
    const TransformComponent& transform,
    float& outDistance,
    Vec3& outNormal) noexcept {
    const Vec3 scale = Max(Abs(transform.worldScale), Vec3{ 0.0001F, 0.0001F, 0.0001F });
    // Center is a local-space position, therefore it follows signed scale
    // (a mirrored transform mirrors an offset collider); dimensions below
    // use absolute scale because a shape extent cannot be negative.
    const Vec3 scaledCenter{
        collider.center.x * transform.worldScale.x,
        collider.center.y * transform.worldScale.y,
        collider.center.z * transform.worldScale.z,
    };
    const Vec3 center = transform.worldPosition + Rotate(transform.worldRotation, scaledCenter);
    const Quat inverseRotation = Inverse(transform.worldRotation);
    const Vec3 localOrigin = Rotate(inverseRotation, origin - center);
    const Vec3 localDirection = Rotate(inverseRotation, direction);
    switch (collider.shape) {
    case ColliderShape::Sphere: {
        const float radius = collider.radius * std::max({ scale.x, scale.y, scale.z });
        if (!IntersectRaySphere(localOrigin, localDirection, maxDistance, Vec3{}, radius, outDistance, outNormal)) {
            return false;
        }
        outNormal = Rotate(transform.worldRotation, outNormal);
        return true;
    }
    case ColliderShape::Capsule: {
        const float radius = collider.radius * std::max(scale.x, scale.z);
        const float halfCylinder = std::max(0.0F, collider.height * scale.y * 0.5F - radius);
        if (!IntersectRayCapsule(localOrigin, localDirection, maxDistance, radius, halfCylinder, outDistance, outNormal)) {
            return false;
        }
        outNormal = Rotate(transform.worldRotation, outNormal);
        return true;
    }
    case ColliderShape::Box:
        if (!IntersectRayAabb(localOrigin, localDirection, maxDistance, Vec3{}, Vec3{
            std::max(0.0001F, collider.boxSize.x * scale.x * 0.5F),
            std::max(0.0001F, collider.boxSize.y * scale.y * 0.5F),
            std::max(0.0001F, collider.boxSize.z * scale.z * 0.5F),
        }, outDistance, outNormal)) {
            return false;
        }
        outNormal = Rotate(transform.worldRotation, outNormal);
        return true;
    }
    return false;
}

void RaycastAllNonAlloc(Scene& scene, Vec3 origin, Vec3 direction, float maxDistance, std::uint32_t layerMask, kb::library::ArrayNonAlloc<PhysicsCastResult>& results) {
    results.Clear();
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
        !std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z) ||
        !std::isfinite(maxDistance) || maxDistance <= 0.0F || layerMask == 0U) {
        return;
    }
    const Vec3 normalizedDirection = Normalize(direction);
    if (kb::math::Length(normalizedDirection) <= 0.000001F || maxDistance <= 0.0F) {
        return;
    }
    RaycastAllVisitorContext context{
        .scene = &scene,
        .origin = origin,
        .direction = normalizedDirection,
        .maxDistance = maxDistance,
        .layerMask = layerMask,
        .results = &results,
    };
    scene.Runtime().SynchronizeTransforms();
    scene.Transforms().ForEach(&RaycastAllVisitor, &context);
    // InsertBoundedCastResult keeps the caller-owned buffer sorted without
    // temporary storage or a post-query allocation.
}

} // namespace kb::scene
