#pragma once

#include "kb/render/scene/SceneRenderTypes.hpp"

#include <cstdint>

namespace kb::render {

class RendererSceneLightingConfigResolver {
public:
    [[nodiscard]] static SceneRenderLightingConfig Resolve(SceneRenderLightingConfig requested, SceneRenderLightingConfig fallback) noexcept;
    [[nodiscard]] static std::uint32_t ShadowFilterSampleCount(SceneRenderShadowFilter filter) noexcept;
    [[nodiscard]] static std::uint32_t EnvironmentSampleCount(SceneRenderEnvironmentMode mode) noexcept;
};

} // namespace kb::render
