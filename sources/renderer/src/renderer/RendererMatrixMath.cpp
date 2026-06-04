#include "renderer/RendererMatrixMath.hpp"

#include <bx/math.h>

namespace kb::render {

std::array<float, 16> RendererMatrixMath::Identity() noexcept {
    return std::array<float, 16>{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
}

std::array<float, 16> RendererMatrixMath::ViewProjection(const SceneRenderCamera& camera) noexcept {
    std::array<float, 16> viewProjection{};
    bx::mtxMul(viewProjection.data(), camera.view.data(), camera.projection.data());
    return viewProjection;
}

std::array<float, 16> RendererMatrixMath::Inverse(const std::array<float, 16>& matrix) noexcept {
    std::array<float, 16> inverse{};
    bx::mtxInverse(inverse.data(), matrix.data());
    return inverse;
}

} // namespace kb::render
