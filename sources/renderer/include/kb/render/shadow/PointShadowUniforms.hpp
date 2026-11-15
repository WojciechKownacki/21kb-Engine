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
    // Graph material shaders own the stages from 6 up for their textures; see pbr_graph_forward.sh.
    static constexpr std::uint8_t kGraphSamplerStage = 1U;

    bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle depth = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle spot = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle atlas = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle info = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle params = BGFX_INVALID_HANDLE;

    void Create() {
        constexpr auto lights = static_cast<std::uint16_t>(ScenePointShadowBinding::kMaxLights);
        sampler = bgfx::createUniform("s_pointShadowMap", bgfx::UniformType::Sampler);
        light = bgfx::createUniform("u_pointShadowLight", bgfx::UniformType::Vec4, lights);
        depth = bgfx::createUniform("u_pointShadowDepth", bgfx::UniformType::Vec4, lights);
        spot = bgfx::createUniform("u_pointShadowSpot", bgfx::UniformType::Vec4, lights);
        atlas = bgfx::createUniform("u_pointShadowAtlas", bgfx::UniformType::Vec4);
        info = bgfx::createUniform("u_pointShadowInfo", bgfx::UniformType::Vec4);
        params = bgfx::createUniform("u_pointShadowParams", bgfx::UniformType::Vec4, lights);
    }

    void Destroy() noexcept {
        for (bgfx::UniformHandle* handle : { &sampler, &light, &depth, &spot, &atlas, &info, &params }) {
            if (bgfx::isValid(*handle)) {
                bgfx::destroy(*handle);
            }
            *handle = BGFX_INVALID_HANDLE;
        }
    }

    [[nodiscard]] bool IsValid() const noexcept {
        return bgfx::isValid(sampler) && bgfx::isValid(light) && bgfx::isValid(depth) &&
            bgfx::isValid(spot) && bgfx::isValid(atlas) && bgfx::isValid(info) && bgfx::isValid(params);
    }

    // Publishes zero lights when `binding` is null or invalid, which the shader treats as "no shadow".
    // lightSlots holds the packed light slot of each shadow light (negative = not packed). `compare` reads the atlas
    // through the comparison sampler of the KB_SHADOW_COMPARE programs (then `fallbackTexture` is a depth texture),
    // `wideKernel` takes their nine-tap soft kernel.
    void Set(const ScenePointShadowBinding* binding, const std::array<float, ScenePointShadowBinding::kMaxLights>& lightSlots,
        bgfx::TextureHandle fallbackTexture, std::uint8_t samplerStage = kSamplerStage, bool compare = false,
        bool wideKernel = true) const {
        const bool enabled = binding != nullptr && binding->IsValid();
        std::array<float, 4U * ScenePointShadowBinding::kMaxLights> lightData{};
        std::array<float, 4U * ScenePointShadowBinding::kMaxLights> depthData{};
        std::array<float, 4U> atlasData{};
        std::array<float, 4U> infoData{};
        const std::array<float, 4U * ScenePointShadowBinding::kMaxLights> noSpot{};
        if (enabled) {
            for (std::uint32_t slot = 0U; slot < binding->lightCount; ++slot) {
                lightData[slot * 4U + 0U] = binding->positionRange[slot * 4U + 0U];
                lightData[slot * 4U + 1U] = binding->positionRange[slot * 4U + 1U];
                lightData[slot * 4U + 2U] = binding->positionRange[slot * 4U + 2U];
                lightData[slot * 4U + 3U] = lightSlots[slot];
                depthData[slot * 4U + 0U] = binding->depthParams[slot * 4U + 0U];
                depthData[slot * 4U + 1U] = binding->positionRange[slot * 4U + 3U];
                depthData[slot * 4U + 2U] = binding->depthParams[slot * 4U + 1U];
                depthData[slot * 4U + 3U] = binding->depthParams[slot * 4U + 2U];
            }
            atlasData = binding->atlas;
            const bgfx::Caps* caps = bgfx::getCaps();
            infoData = { static_cast<float>(binding->lightCount), caps != nullptr && caps->originBottomLeft ? 0.5F : -0.5F, binding->strength,
                wideKernel ? 1.0F : 0.0F };
        }
        // A comparison sampler needs a depth texture even when no light has a shadow (the shader never reads it then):
        // the caller passes the other map as the fallback.
        if (compare) {
            bgfx::setTexture(samplerStage, sampler, enabled ? binding->depthTexture : fallbackTexture, kShadowCompareSamplerFlags);
        } else {
            bgfx::setTexture(samplerStage, sampler, enabled ? binding->depthTexture : fallbackTexture);
        }
        bgfx::setUniform(light, lightData.data(), ScenePointShadowBinding::kMaxLights);
        bgfx::setUniform(depth, depthData.data(), ScenePointShadowBinding::kMaxLights);
        bgfx::setUniform(spot, enabled ? binding->spot.data() : noSpot.data(), ScenePointShadowBinding::kMaxLights);
        bgfx::setUniform(atlas, atlasData.data());
        bgfx::setUniform(info, infoData.data());
        bgfx::setUniform(params, enabled ? binding->lightParams.data() : noSpot.data(), ScenePointShadowBinding::kMaxLights);
    }
};

} // namespace kb::render
