#pragma once

#include "engine/scene/TransformComponent.hpp"

namespace kb::scene {

enum class RigidbodyBodyType {
    Static,
    Dynamic,
    Kinematic,
};

struct RigidbodyComponent {
    RigidbodyBodyType bodyType = RigidbodyBodyType::Dynamic;
    float mass = 1.0F;
    Vec3 linearVelocity{};
    Vec3 angularVelocity{};
    float gravityScale = 1.0F;
    bool useGravity = true;
    bool lockRotation = false;
    // LIB-133: opts this body into continuous collision detection (Jolt's
    // EMotionQuality::LinearCast, a swept test against the path traveled this step) instead of
    // the default discrete "teleport then check overlap" test, which can tunnel a fast-moving
    // body clean through a thin collider within a single fixed step. Off by default - zero
    // behavior change for existing content, matching Jolt's own EMotionQuality::Discrete
    // default - since LinearCast costs real extra CPU and most bodies never move fast enough
    // relative to their own/nearby colliders' thickness to need it.
    bool useContinuousCollision = false;
};

} // namespace kb::scene
