#pragma once

#include "kb/render/gi/SceneGiHistory.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

#include <array>

namespace kb::render {

// Whether the bounce light is gathered by the resolve pass (screen-space or voxel GI).
[[nodiscard]] constexpr bool UsesGatheredGi(const SceneRenderLightingConfig& config) noexcept {
    return config.globalIllumination == SceneRenderGlobalIlluminationMode::SsGi ||
        config.globalIllumination == SceneRenderGlobalIlluminationMode::VoxelGrid;
}

// Whether the frame needs the screen-space resolve (GI, ambient occlusion) or the previous frame's lit
// colour (reflections). All of them read the G-buffer, so they imply the deferred path.
[[nodiscard]] constexpr bool UsesScreenSpaceEffects(const SceneRenderLightingConfig& config) noexcept {
    return UsesGatheredGi(config) || config.ambientOcclusionEnabled || config.screenSpaceReflectionsEnabled;
}

// The uniforms of ssgi.sh, ssao.sh and ssr.sh, shared by the resolve pass (writes the accumulated
// results) and the deferred lighting pass (reads them).
class SceneGiUniforms {
public:
    static constexpr std::uint8_t kAccumStage = 8U;
    static constexpr std::uint8_t kAoStage = 10U;
    static constexpr std::uint8_t kLitStage = 11U;

    void Create();
    void Destroy() noexcept;
    [[nodiscard]] bool IsValid() const noexcept;
    // `binding` may be null; `unavailableTexture` stands in for the textures then. Each effect's
    // intensity is zero unless it is enabled, which switches its shader path off.
    void Set(const SceneGiBinding* binding, const SceneRenderLightingConfig& config,
        const std::array<float, 16>& viewProjection, bgfx::TextureHandle unavailableTexture) const;

private:
    bgfx::UniformHandle accumSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle aoSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle litSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle viewProjUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle prevViewProjUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle paramsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle temporalUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle aoParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ssrParamsUniform_ = BGFX_INVALID_HANDLE;
};

} // namespace kb::render
