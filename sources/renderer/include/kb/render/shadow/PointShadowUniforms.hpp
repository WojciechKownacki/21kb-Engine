#pragma once

#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

namespace kb::render {

// Uniforms and sampler read by point_shadow.sh. The forward and deferred lighting passes each own a
// reference; bgfx resolves equal names to one handle.
struct PointShadowUniforms {
    static constexpr std::uint8_t kSamplerStage = 9U;

    bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle depth = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle atlas = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle info = BGFX_INVALID_HANDLE;

    void Create() {
        constexpr auto lights = static_cast<std::uint16_t>(ScenePointShadowBinding::kMaxLights);
        sampler = bgfx::createUniform("s_pointShadowMap", bgfx::UniformType::Sampler);
        light = bgfx::createUniform("u_pointShadowLight", bgfx::UniformType::Vec4, lights);
        depth = bgfx::createUniform("u_pointShadowDepth", bgfx::UniformType::Vec4, lights);
        atlas = bgfx::createUniform("u_pointShadowAtlas", bgfx::UniformType::Vec4);
        info = bgfx::createUniform("u_pointShadowInfo", bgfx::UniformType::Vec4);
    }

    void Destroy() noexcept {
        for (bgfx::UniformHandle* handle : { &sampler, &light, &depth, &atlas, &info }) {
            if (bgfx::isValid(*handle)) {
                bgfx::destroy(*handle);
            }
            *handle = BGFX_INVALID_HANDLE;
        }
    }

    [[nodiscard]] bool IsValid() const noexcept {
        return bgfx::isValid(sampler) && bgfx::isValid(light) && bgfx::isValid(depth) &&
            bgfx::isValid(atlas) && bgfx::isValid(info);
    }

    // Publishes zero lights when `binding` is null or invalid, which the shader treats as "no shadow".
    // lightSlots holds the packed light slot of each shadow light (negative = not packed).
    void Set(const ScenePointShadowBinding* binding, const std::array<float, ScenePointShadowBinding::kMaxLights>& lightSlots,
        bgfx::TextureHandle fallbackTexture) const {
        const bool enabled = binding != nullptr && binding->IsValid();
        std::array<float, 4U * ScenePointShadowBinding::kMaxLights> lightData{};
        std::array<float, 4U * ScenePointShadowBinding::kMaxLights> depthData{};
        std::array<float, 4U> atlasData{};
        std::array<float, 4U> infoData{};
        if (enabled) {
            for (std::uint32_t slot = 0U; slot < binding->lightCount; ++slot) {
                lightData[slot * 4U + 0U] = binding->positionRange[slot * 4U + 0U];
                lightData[slot * 4U + 1U] = binding->positionRange[slot * 4U + 1U];
                lightData[slot * 4U + 2U] = binding->positionRange[slot * 4U + 2U];
                lightData[slot * 4U + 3U] = lightSlots[slot];
                depthData[slot * 4U + 0U] = binding->depthParams[slot * 4U + 0U];
                depthData[slot * 4U + 1U] = binding->positionRange[slot * 4U + 3U];
                depthData[slot * 4U + 2U] = binding->depthParams[slot * 4U + 1U];
            }
            atlasData = binding->atlas;
            const bgfx::Caps* caps = bgfx::getCaps();
            infoData = { static_cast<float>(binding->lightCount), caps != nullptr && caps->originBottomLeft ? 0.5F : -0.5F, binding->strength, 0.0F };
        }
        bgfx::setTexture(kSamplerStage, sampler, enabled ? binding->depthTexture : fallbackTexture);
        bgfx::setUniform(light, lightData.data(), ScenePointShadowBinding::kMaxLights);
        bgfx::setUniform(depth, depthData.data(), ScenePointShadowBinding::kMaxLights);
        bgfx::setUniform(atlas, atlasData.data());
        bgfx::setUniform(info, infoData.data());
    }
};

} // namespace kb::render
