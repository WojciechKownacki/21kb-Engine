#pragma once

#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/TransformComponent.hpp"

namespace kb::scene {

// Pure ColliderComponent/TransformComponent ray-vs-shape geometry - shared
// by kb::script::ScriptPhysicsApi's single-hit Physics.Raycast
// (ScriptPhysicsApi.cpp) and kb::scene::RaycastAllNonAlloc
// (PhysicsGeometryQueries.cpp, LIB-126) so the two can never silently drift
// apart into different hit results for the same ray.
[[nodiscard]] bool IntersectRayCollider(
    Vec3 origin,
    Vec3 direction,
    float maxDistance,
    const ColliderComponent& collider,
    const TransformComponent& transform,
    float& outDistance,
    Vec3& outNormal) noexcept;

} // namespace kb::scene
