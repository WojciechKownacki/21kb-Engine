#pragma once

#include "kb/render/scene/SceneRenderTypes.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace kb::render {

// Two local cubes fit alongside the existing material and deferred-lighting samplers.
inline constexpr std::uint32_t kMaxSampledLocalReflectionProbes = 2U;

[[nodiscard]] inline bool ValidLocalReflectionConfig(const SceneRenderIblConfig& config) noexcept {
    if (config.reflectionProbeCount > kMaxSampledLocalReflectionProbes) return false;
    for (std::uint32_t i = 0; i < config.reflectionProbeCount; ++i) {
        const auto& probe = config.reflectionProbes[i];
        if (probe.shape != SceneRenderReflectionProbeShape::Infinite && probe.shape != SceneRenderReflectionProbeShape::Box && probe.shape != SceneRenderReflectionProbeShape::Sphere) return false;
        if (!bgfx::isValid(probe.radianceTexture) || probe.maxMip == 0U) return false;
        for (unsigned axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(probe.position[axis]) || !std::isfinite(probe.extents[axis]) || probe.extents[axis] <= 0) return false;
        }
        if (!std::isfinite(probe.intensity) || probe.intensity < 0.0F || (probe.shape == SceneRenderReflectionProbeShape::Sphere &&
            (!std::isfinite(probe.influenceRadius) || probe.influenceRadius <= 0))) return false;
    }
    return true;
}

class LocalReflectionUniforms final {
public:
    [[nodiscard]] bool Initialize() {
        if (IsInitialized()) return true;
        samplers_[0] = bgfx::createUniform("s_localReflection0",bgfx::UniformType::Sampler);
        samplers_[1] = bgfx::createUniform("s_localReflection1",bgfx::UniformType::Sampler);
        positions_ = bgfx::createUniform("u_localReflectionPositions",bgfx::UniformType::Vec4,2);
        extents_ = bgfx::createUniform("u_localReflectionExtents",bgfx::UniformType::Vec4,2);
        parameters_ = bgfx::createUniform("u_localReflectionParameters",bgfx::UniformType::Vec4,2);
        control_ = bgfx::createUniform("u_localReflectionControl",bgfx::UniformType::Vec4);
        if (!IsInitialized()) { Shutdown(); return false; }
        return true;
    }
    void Shutdown() noexcept {
        for (auto* handle : {&samplers_[0],&samplers_[1],&positions_,&extents_,&parameters_,&control_}) {
            if (bgfx::isValid(*handle)) { bgfx::destroy(*handle); *handle = BGFX_INVALID_HANDLE; }
        }
    }
    [[nodiscard]] bool IsInitialized() const noexcept {
        return bgfx::isValid(samplers_[0]) && bgfx::isValid(samplers_[1]) && bgfx::isValid(positions_) &&
            bgfx::isValid(extents_) && bgfx::isValid(parameters_) && bgfx::isValid(control_);
    }
    void Set(const SceneRenderIblConfig& config, bgfx::TextureHandle fallback, std::uint8_t firstStage, std::uint8_t secondStage) const {
        std::array<float,8> positions{}, extents{}, parameters{};
        const auto count = std::min(config.reflectionProbeCount,kMaxSampledLocalReflectionProbes);
        for (unsigned i = 0; i < 2; ++i) {
            const bool enabled = i < count && config.reflectionProbes[i].IsValid();
            const auto& probe = config.reflectionProbes[i];
            bgfx::setTexture(i == 0 ? firstStage : secondStage,samplers_[i],enabled ? probe.radianceTexture : fallback);
            for (unsigned axis = 0; axis < 3; ++axis) {
                positions[i*4+axis] = probe.position[axis];
                extents[i*4+axis] = probe.extents[axis];
            }
            positions[i*4+3] = enabled ? probe.intensity : 0.0F;
            extents[i*4+3] = static_cast<float>(probe.shape);
            parameters[i*4] = static_cast<float>(probe.maxMip > 0 ? probe.maxMip - 1U : 0U);
            parameters[i*4+1] = probe.influenceRadius;
            parameters[i*4+2] = probe.parallaxCorrection && config.parallaxCorrection ? 1.0F : 0.0F;
        }
        const std::array<float,4> control{static_cast<float>(count),config.localProbeBlending ? 1.0F : 0.0F,0,0};
        bgfx::setUniform(positions_,positions.data(),2);
        bgfx::setUniform(extents_,extents.data(),2);
        bgfx::setUniform(parameters_,parameters.data(),2);
        bgfx::setUniform(control_,control.data());
    }
private:
    std::array<bgfx::UniformHandle,2> samplers_{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
    bgfx::UniformHandle positions_ = BGFX_INVALID_HANDLE, extents_ = BGFX_INVALID_HANDLE,
        parameters_ = BGFX_INVALID_HANDLE, control_ = BGFX_INVALID_HANDLE;
};

} // namespace kb::render
