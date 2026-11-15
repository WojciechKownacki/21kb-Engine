#pragma once

#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

namespace kb::render {

class RenderResourceRegistry;
class RenderScene;
class SceneRenderResourceMap;

struct DirectionalShadowSetup {
    SceneRenderCamera camera{};
    std::array<SceneRenderCamera, SceneRenderShadowMapBinding::kMaxCascades> cascadeCameras{};
    std::uint32_t atlasSize = 0;
    SceneRenderShadowMapBinding binding{};
    std::uint64_t lightEntityId = 0;
    std::uint32_t casterCount = 0;
    // Half the width of each cascade's orthographic window (m).
    std::array<float, SceneRenderShadowMapBinding::kMaxCascades> cascadeHalfExtent{};
    // Whether the cascades can be kept between frames: false while a caster's material moves its surface by itself.
    bool cacheable = false;
    bool valid = false;
};

class DirectionalShadowPassPlanner {
public:
    [[nodiscard]] DirectionalShadowSetup Build(
        const RenderScene& renderScene,
        const RenderResourceRegistry& resources,
        const SceneRenderResourceMap& resourceMap,
        SceneRenderLightingConfig lightingConfig,
        bgfx::TextureHandle shadowDepthTexture,
        std::uint32_t cameraCullingMask = 0xFFFFFFFFU,
        const std::array<float, 3>* cameraPosition = nullptr,
        std::uint32_t maxCascades = SceneRenderShadowMapBinding::kMaxCascades) const noexcept;
};

} // namespace kb::render
