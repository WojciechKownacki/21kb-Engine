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

struct RaycastAllVisitorContext {
    Scene* scene = nullptr;
    Vec3 origin{};
    Vec3 direction{};
    float maxDistance = 0.0F;
    std::uint32_t layerMask = kPhysicsAllLayers;
    kb::library::ArrayNonAlloc<PhysicsCastResult>* results = nullptr;
};

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
    // Buffer-full is not an error (LIB-126's documented NonAlloc contract -
    // PushBack returning false here just means this hit does not fit;
    // remaining colliders are still visited so a later, closer one is never
    // skipped in favor of a farther one already collected before sorting).
    [[maybe_unused]] const bool pushed = context->results->PushBack(PhysicsCastResult{
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
    const Vec3 center = transform.worldPosition + Vec3{ collider.center.x * scale.x, collider.center.y * scale.y, collider.center.z * scale.z };
    switch (collider.shape) {
    case ColliderShape::Sphere: {
        const float radius = collider.radius * std::max({ scale.x, scale.y, scale.z });
        return IntersectRaySphere(origin, direction, maxDistance, center, radius, outDistance, outNormal);
    }
    case ColliderShape::Capsule: {
        const float radius = collider.radius * std::max(scale.x, scale.z);
        const float halfHeight = std::max(0.0F, collider.height * scale.y * 0.5F - radius);
        float bestDistance = std::numeric_limits<float>::max();
        Vec3 bestNormal{};
        bool hit = false;
        for (Vec3 sphereCenter : { center + Vec3{ 0.0F, halfHeight, 0.0F }, center - Vec3{ 0.0F, halfHeight, 0.0F } }) {
            float candidateDistance = 0.0F;
            Vec3 candidateNormal{};
            if (IntersectRaySphere(origin, direction, maxDistance, sphereCenter, radius, candidateDistance, candidateNormal) && candidateDistance < bestDistance) {
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
    case ColliderShape::Box:
        return IntersectRayAabb(origin, direction, maxDistance, center, Vec3{
            std::max(0.0001F, collider.boxSize.x * scale.x * 0.5F),
            std::max(0.0001F, collider.boxSize.y * scale.y * 0.5F),
            std::max(0.0001F, collider.boxSize.z * scale.z * 0.5F),
        }, outDistance, outNormal);
    }
    return false;
}

void RaycastAllNonAlloc(Scene& scene, Vec3 origin, Vec3 direction, float maxDistance, std::uint32_t layerMask, kb::library::ArrayNonAlloc<PhysicsCastResult>& results) {
    results.Clear();
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
    // Closest-first, matching the Jolt-backed CastShapeAll/OverlapShapeAll
    // convention (JPH::AllHitCollisionCollector::Sort()) - results.begin()/
    // end() are mutable span iterators even though the accessor is `const`
    // (ArrayNonAlloc wraps a std::span<T>, whose constness never propagates
    // to its pointees), so std::sort works directly over the caller's
    // buffer with no extra copy.
    std::sort(results.begin(), results.end(), [](const PhysicsCastResult& lhs, const PhysicsCastResult& rhs) { return lhs.distance < rhs.distance; });
}

} // namespace kb::scene
