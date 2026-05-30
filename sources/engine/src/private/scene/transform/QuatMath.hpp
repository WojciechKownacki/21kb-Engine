#pragma once

#include "engine/scene/TransformComponent.hpp"

namespace kb::scene {

class QuatMath {
public:
    QuatMath() = delete;

    [[nodiscard]] static Quat Multiply(Quat lhs, Quat rhs) noexcept;
    [[nodiscard]] static Quat Normalize(Quat value) noexcept;
};

} // namespace kb::scene
