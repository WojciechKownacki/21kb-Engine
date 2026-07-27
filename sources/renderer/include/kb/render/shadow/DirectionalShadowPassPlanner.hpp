#pragma once

#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace kb::render {

class RenderResourceRegistry;
class RenderScene;
class SceneRenderResourceMap;

struct DirectionalShadowSetup {
    SceneRenderCamera camera{};
    SceneRenderShadowMapBinding binding{};
    std::uint64_t lightEntityId = 0;
    std::uint32_t casterCount = 0;
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
        std::uint32_t cameraCullingMask = 0xFFFFFFFFU) const noexcept;
};

} // namespace kb::render
