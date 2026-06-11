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
};

} // namespace kb::scene
