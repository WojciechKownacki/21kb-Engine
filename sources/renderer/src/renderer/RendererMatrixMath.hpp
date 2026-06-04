#pragma once

#include "kb/render/scene/SceneRenderTypes.hpp"

#include <array>

namespace kb::render {

class RendererMatrixMath {
public:
    [[nodiscard]] static std::array<float, 16> Identity() noexcept;
    [[nodiscard]] static std::array<float, 16> ViewProjection(const SceneRenderCamera& camera) noexcept;
    [[nodiscard]] static std::array<float, 16> Inverse(const std::array<float, 16>& matrix) noexcept;
};

} // namespace kb::render
