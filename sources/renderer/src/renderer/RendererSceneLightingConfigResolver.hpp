#pragma once

#include "kb/render/post/ScenePostProcessSettings.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <cstdint>

namespace kb::render {

class RendererSceneLightingConfigResolver {
public:
    [[nodiscard]] static SceneRenderLightingConfig Resolve(SceneRenderLightingConfig requested, SceneRenderLightingConfig fallback) noexcept;
    // The scene's post-process profile owns ambient occlusion and turns on reflections,
    // global illumination and fog over the lighting config's own.
    static void ApplyPostProcessLighting(SceneRenderLightingConfig& config, const ScenePostProcessSettings& settings) noexcept;
    // Resolve target defaults before submission; explicit profiles also allow desktop previews.
    static void ResolveFogProfile(SceneRenderLightingConfig& config, bgfx::RendererType::Enum backend) noexcept;
    [[nodiscard]] static std::uint32_t ShadowFilterSampleCount(SceneRenderShadowFilter filter) noexcept;
    [[nodiscard]] static std::uint32_t EnvironmentSampleCount(SceneRenderEnvironmentMode mode) noexcept;
};

} // namespace kb::render
