#pragma once

#include "kb/render/frame/RenderViewportDesc.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <array>
#include <cstdint>

namespace kb::render {

class RendererTemporalJitter {
public:
    [[nodiscard]] static std::array<float, 2> Compute(std::uint64_t frameIndex, RenderExtent extent, bool enabled) noexcept;
    static void Apply(SceneRenderCamera& camera, std::array<float, 2> jitter) noexcept;
};

} // namespace kb::render
