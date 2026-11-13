#pragma once

#include "kb/render/resources/RenderResources.hpp"
#include "kb/render/scene/RenderScene.hpp"

#include <cstdint>
#include <limits>

namespace kb::render {

class RenderResourceRegistry;
class SceneRenderResourceMap;

struct ShadowCasterBounds {
    RenderBoundsSphere bounds{};
    std::uint32_t casterCount = 0;
    float focusedDepthMinimum = std::numeric_limits<float>::max();
    float focusedDepthMaximum = std::numeric_limits<float>::lowest();
    std::uint32_t focusedCasterCount = 0U;
    // Casters whose material may move their surface by itself (World Position Offset), within the focus when there
    // is one: their shadow changes without a change of the scene, so a cached shadow cannot hold them.
    std::uint32_t displacedCasterCount = 0U;
};

class ShadowCasterBoundsCollector {
public:
    ShadowCasterBoundsCollector() = delete;

    [[nodiscard]] static ShadowCasterBounds Collect(
        const RenderScene& renderScene,
        const RenderResourceRegistry& resources,
        const SceneRenderResourceMap& resourceMap,
        std::uint32_t cameraCullingMask = 0xFFFFFFFFU,
        const std::array<float, 16>* focusView = nullptr,
        float focusHalfExtent = 0.0F) noexcept;
};

} // namespace kb::render
