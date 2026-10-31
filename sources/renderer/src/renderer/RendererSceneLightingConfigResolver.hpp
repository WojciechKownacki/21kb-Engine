#pragma once

#include "kb/render/post/ScenePostProcessSettings.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <cstdint>

namespace kb::render {

class RendererSceneLightingConfigResolver {
public:
    [[nodiscard]] static SceneRenderLightingConfig Resolve(SceneRenderLightingConfig requested, SceneRenderLightingConfig fallback) noexcept;
    // The scene's post-process profile owns screen-space ambient occlusion, as Unity's Volume
    // (SSAO override) and Unreal's post-process volume do: it switches the renderer's SSAO pass
    // on or off and sets its intensity and radius.
    static void ApplyPostProcessAmbientOcclusion(SceneRenderLightingConfig& config, const ScenePostProcessSettings& settings) noexcept;
    // Resolve target defaults before submission; explicit profiles also allow desktop previews.
    static void ResolveFogProfile(SceneRenderLightingConfig& config, bgfx::RendererType::Enum backend) noexcept;
    [[nodiscard]] static std::uint32_t ShadowFilterSampleCount(SceneRenderShadowFilter filter) noexcept;
    [[nodiscard]] static std::uint32_t EnvironmentSampleCount(SceneRenderEnvironmentMode mode) noexcept;
};

} // namespace kb::render
