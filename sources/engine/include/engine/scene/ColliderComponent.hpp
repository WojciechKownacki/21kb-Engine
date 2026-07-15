#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <cstdint>

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
    // LIB-125: raw collision layer bitmask this collider belongs to, tested
    // against a query's layerMask with a plain bitwise AND
    // (kb::scene::PhysicsBackend::CastShape/OverlapShape). Matches every
    // layer by default (kb::scene::kPhysicsAllLayers - 31 bits, not 32, see
    // that constant's own comment for why) so existing content and existing
    // queries are unaffected until something deliberately narrows one side.
    // LIB-129 owns turning this into named, asset-configurable layers and an
    // interaction matrix - this field is intentionally just the raw bitmask
    // LIB-125 needs to make "z warstwa maski" real today.
    std::uint32_t layer = 0x7FFFFFFFU;
};

} // namespace kb::scene
