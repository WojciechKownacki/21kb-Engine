#pragma once

#include "engine/scene/TransformComponent.hpp"

namespace kb::scene {

enum class ColliderShape {
    Box,
    Sphere,
    Capsule,
};

struct ColliderComponent {
    ColliderShape shape = ColliderShape::Box;
    Vec3 center{};
    Vec3 boxSize{ 1.0F, 1.0F, 1.0F };
    float radius = 0.5F;
    float height = 2.0F;
    bool trigger = false;
    // LIB-123: PhysicsMaterial, embedded rather than a separate asset - every
    // collider needs exactly one friction/restitution pair and nothing in
    // this plan asks for multiple colliders to share and hot-reload a single
    // material, so a reusable asset type would be unused machinery.
    float friction = 0.5F;
    float restitution = 0.0F;
};

} // namespace kb::scene
