#pragma once

#include "scene/lighting/SceneLightShaderData.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace kb::render {

// Derived world-space CSR light lists. Cell zero contains only unbounded lights;
// the remaining cells conservatively include every bounded light influencing them.
class SceneLightGrid {
public:
    [[nodiscard]] bool Build(const RenderScene::LightProxyMap& lights,
        SceneRenderLightingConfig config, std::uint32_t cameraMask,
        std::uint64_t primaryLightId, std::uint32_t maxAtlasTexels = 1U << 20U);
    [[nodiscard]] std::span<const std::array<float, 4>> Atlas() const noexcept { return atlas_; }
    [[nodiscard]] const std::array<float, 4>& Minimum() const noexcept { return minimum_; }
    [[nodiscard]] const std::array<float, 4>& InverseCellSize() const noexcept { return inverseCellSize_; }
    [[nodiscard]] const std::array<float, 4>& Dimensions() const noexcept { return dimensions_; }
    [[nodiscard]] std::uint32_t LightCount() const noexcept { return lightCount_; }
    [[nodiscard]] std::uint32_t HeaderOffset() const noexcept { return lightCount_ * 5U; }
    [[nodiscard]] std::uint32_t IndexOffset() const noexcept { return HeaderOffset() + cellCount_ + 1U; }
    [[nodiscard]] std::uint32_t CellFor(const std::array<float, 3>& position) const noexcept;

private:
    std::vector<std::array<float, 4>> atlas_;
    std::vector<std::uint32_t> counts_;
    std::array<float, 4> minimum_{};
    std::array<float, 4> inverseCellSize_{};
    std::array<float, 4> dimensions_{};
    std::uint32_t lightCount_ = 0U;
    std::uint32_t cellCount_ = 0U;
};

} // namespace kb::render
