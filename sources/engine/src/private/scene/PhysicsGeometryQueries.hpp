#pragma once

#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/TransformComponent.hpp"

namespace kb::scene {

// Authored primitive geometry for ray queries without an active physics backend.
[[nodiscard]] bool IntersectRayCollider(
    Vec3 origin,
    Vec3 direction,
    float maxDistance,
    const ColliderComponent& collider,
    const TransformComponent& transform,
    float& outDistance,
    Vec3& outNormal) noexcept;

} // namespace kb::scene
