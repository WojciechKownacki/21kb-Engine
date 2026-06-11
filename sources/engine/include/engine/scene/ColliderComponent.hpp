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
};

} // namespace kb::scene
