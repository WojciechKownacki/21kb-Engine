#pragma once

#include "kb/render/resources/RenderResources.hpp"
#include "kb/render/scene/RenderScene.hpp"

#include <cstdint>

namespace kb::render {

class RenderResourceRegistry;
class SceneRenderResourceMap;

struct ShadowCasterBounds {
    RenderBoundsSphere bounds{};
    std::uint32_t casterCount = 0;
};

class ShadowCasterBoundsCollector {
public:
    ShadowCasterBoundsCollector() = delete;

    [[nodiscard]] static ShadowCasterBounds Collect(
        const RenderScene& renderScene,
        const RenderResourceRegistry& resources,
        const SceneRenderResourceMap& resourceMap) noexcept;
};

} // namespace kb::render
