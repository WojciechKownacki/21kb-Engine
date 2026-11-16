#pragma once

#include "kb/render/scene/SceneRenderTypes.hpp"

#include <array>
#include <cstdint>

namespace kb::render {

// The sampler stage and uniforms of planar_reflection.sh. The lighting shaders read one mirror per view; the
// stage is free in the forward mesh shader (13) and in the deferred lighting shader (15).
class PlanarReflectionUniforms final {
public:
    static constexpr std::uint8_t kForwardStage = 13U;
    static constexpr std::uint8_t kDeferredStage = 15U;

    [[nodiscard]] bool Initialize() {
        if (IsInitialized()) return true;
        sampler_ = bgfx::createUniform("s_planarReflection", bgfx::UniformType::Sampler);
        viewProjection_ = bgfx::createUniform("u_planarViewProj", bgfx::UniformType::Mat4);
        plane_ = bgfx::createUniform("u_planarPlane", bgfx::UniformType::Vec4);
        center_ = bgfx::createUniform("u_planarCenter", bgfx::UniformType::Vec4);
        right_ = bgfx::createUniform("u_planarRight", bgfx::UniformType::Vec4);
        forward_ = bgfx::createUniform("u_planarForward", bgfx::UniformType::Vec4);
        parameters_ = bgfx::createUniform("u_planarParams", bgfx::UniformType::Vec4);
        // Bound while no mirror is ready, so the stage never holds a texture of another kind.
        const std::array<std::uint8_t, 4> black{0U, 0U, 0U, 255U};
        fallback_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
            bgfx::copy(black.data(), static_cast<std::uint32_t>(black.size())));
        if (!IsInitialized()) { Shutdown(); return false; }
        return true;
    }
    void Shutdown() noexcept {
        if (bgfx::isValid(fallback_)) { bgfx::destroy(fallback_); fallback_ = BGFX_INVALID_HANDLE; }
        for (auto* handle : {&sampler_, &viewProjection_, &plane_, &center_, &right_, &forward_, &parameters_}) {
            if (bgfx::isValid(*handle)) { bgfx::destroy(*handle); *handle = BGFX_INVALID_HANDLE; }
        }
    }
    [[nodiscard]] bool IsInitialized() const noexcept {
        return bgfx::isValid(sampler_) && bgfx::isValid(viewProjection_) && bgfx::isValid(plane_) && bgfx::isValid(center_) &&
            bgfx::isValid(right_) && bgfx::isValid(forward_) && bgfx::isValid(parameters_) && bgfx::isValid(fallback_);
    }
    // Without a mirror the shader's weight is 0 and the stage holds a placeholder.
    void Set(const SceneRenderPlanarReflection& mirror, std::uint8_t stage) const {
        const bool ready = mirror.IsValid();
        bgfx::setTexture(stage, sampler_, ready ? mirror.texture : fallback_);
        const std::array<float, 4> plane{mirror.normal[0], mirror.normal[1], mirror.normal[2], 0.0F};
        const std::array<float, 4> center{mirror.center[0], mirror.center[1], mirror.center[2], 0.0F};
        const std::array<float, 4> right{mirror.right[0], mirror.right[1], mirror.right[2], mirror.halfWidth};
        const std::array<float, 4> forward{mirror.forward[0], mirror.forward[1], mirror.forward[2], mirror.halfDepth};
        const std::array<float, 4> parameters{ready ? 1.0F : 0.0F, ready ? 1.0F / static_cast<float>(mirror.extent[0]) : 0.0F,
            ready ? 1.0F / static_cast<float>(mirror.extent[1]) : 0.0F, 0.0F};
        bgfx::setUniform(viewProjection_, mirror.viewProjection.data());
        bgfx::setUniform(plane_, plane.data());
        bgfx::setUniform(center_, center.data());
        bgfx::setUniform(right_, right.data());
        bgfx::setUniform(forward_, forward.data());
        bgfx::setUniform(parameters_, parameters.data());
    }

private:
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE, viewProjection_ = BGFX_INVALID_HANDLE, plane_ = BGFX_INVALID_HANDLE,
        center_ = BGFX_INVALID_HANDLE, right_ = BGFX_INVALID_HANDLE, forward_ = BGFX_INVALID_HANDLE, parameters_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fallback_ = BGFX_INVALID_HANDLE;
};

} // namespace kb::render
