#pragma once

#include "engine/scene/TransformComponent.hpp"

namespace kb::scene {

class Vec3Math {
public:
    Vec3Math() = delete;

    [[nodiscard]] static Vec3 Add(Vec3 lhs, Vec3 rhs) noexcept;
    [[nodiscard]] static Vec3 Multiply(Vec3 lhs, Vec3 rhs) noexcept;
    [[nodiscard]] static Vec3 Rotate(Quat rotation, Vec3 value) noexcept;
};

} // namespace kb::scene
