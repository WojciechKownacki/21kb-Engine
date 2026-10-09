#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <array>

namespace kb::render {

class SceneTransformMatrices {
public:
    SceneTransformMatrices() = delete;

    [[nodiscard]] static std::array<float, 16> Model(const kb::scene::TransformComponent& transform) noexcept;
    // The model matrix with the given translation (the camera-relative one the GPU uses).
    [[nodiscard]] static std::array<float, 16> Model(const kb::scene::TransformComponent& transform, kb::math::Vec3 translation) noexcept;
    [[nodiscard]] static float ForwardX(const kb::scene::Quat& rotation) noexcept;
    [[nodiscard]] static float ForwardY(const kb::scene::Quat& rotation) noexcept;
    [[nodiscard]] static float ForwardZ(const kb::scene::Quat& rotation) noexcept;
    [[nodiscard]] static float UpX(const kb::scene::Quat& rotation) noexcept;
    [[nodiscard]] static float UpY(const kb::scene::Quat& rotation) noexcept;
    [[nodiscard]] static float UpZ(const kb::scene::Quat& rotation) noexcept;
};

} // namespace kb::render
