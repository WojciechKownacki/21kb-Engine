#pragma once

#include "kb/render/post/SceneExposureMeter.hpp"

namespace kb::render {

class SceneExposureHistogramBuilder final {
public:
    static constexpr float kMinAverageLuminance = 0.0001F;
    static constexpr float kMaxAverageLuminance = 100000.0F;
    static constexpr float kHdrReadbackMinLog2Luminance = -12.0F;
    static constexpr float kHdrReadbackMaxLog2Luminance = 16.0F;

    [[nodiscard]] static float EstimateAverageLuminance(const RenderScene& scene, const SceneRenderLightingConfig& lightingConfig) noexcept;
    [[nodiscard]] static SceneExposureHistogram BuildLightingHistogram(const RenderScene& scene, const SceneRenderLightingConfig& lightingConfig) noexcept;
    [[nodiscard]] static SceneExposureHistogram BuildHdrReadbackHistogram(std::span<const std::uint8_t> rgba8Pixels) noexcept;
    [[nodiscard]] static SceneExposureHistogram BuildGpuHistogramReadbackHistogram(std::span<const std::uint8_t> rgba8Pixels) noexcept;
    [[nodiscard]] static float MeterAverageLuminance(const SceneExposureHistogram& histogram, SceneExposureMeteringDesc desc = {}) noexcept;
};

} // namespace kb::render
