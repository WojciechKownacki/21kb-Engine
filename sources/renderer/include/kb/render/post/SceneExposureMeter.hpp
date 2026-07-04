#pragma once

#include "kb/render/frame/RenderTargetDesc.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>

namespace kb::render {

class SceneExposureGpuReadback;

struct SceneExposureHistogram {
    static constexpr std::uint32_t kBinCount = 64U;

    std::array<float, kBinCount> bins{};
    float minLog2Luminance = -12.0F;
    float maxLog2Luminance = 16.0F;
    float totalWeight = 0.0F;
};

struct SceneExposureMeteringDesc {
    float lowPercentile = 0.5F;
    float highPercentile = 0.95F;
};

struct SceneExposureAdaptationDesc {
    bool enabled = true;
    float deltaSeconds = 1.0F / 60.0F;
    float brightAdaptationRate = 4.0F;
    float darkAdaptationRate = 1.5F;
};

struct SceneHdrExposureReadbackDesc {
    bgfx::ViewId viewId = 0;
    bgfx::TextureHandle hdrColor = BGFX_INVALID_HANDLE;
    RenderExtent extent{};
    std::uint32_t completedFrame = 0;

    [[nodiscard]] bool IsValid() const noexcept;
};

struct SceneHdrExposureReadbackResult {
    bool submitted = false;
    bool sampleAvailable = false;
    bool hasValidSample = false;
    float meteredAverageLuminance = 0.18F;
};

class SceneExposureMeter {
public:
    static constexpr std::uint32_t kHdrReadbackWidth = 16U;
    static constexpr std::uint32_t kHdrReadbackHeight = 16U;
    static constexpr std::uint32_t kHdrReadbackPixelCount = kHdrReadbackWidth * kHdrReadbackHeight;
    static constexpr std::uint32_t kHdrReadbackBytesPerPixel = 4U;
    static constexpr std::uint32_t kHdrReadbackByteCount = kHdrReadbackPixelCount * kHdrReadbackBytesPerPixel;
    static constexpr std::uint32_t kGpuHistogramReadbackWidth = SceneExposureHistogram::kBinCount;
    static constexpr std::uint32_t kGpuHistogramReadbackHeight = 1U;
    static constexpr std::uint32_t kGpuHistogramReadbackPixelCount = kGpuHistogramReadbackWidth * kGpuHistogramReadbackHeight;
    static constexpr std::uint32_t kGpuHistogramReadbackByteCount = kGpuHistogramReadbackPixelCount * kHdrReadbackBytesPerPixel;

    [[nodiscard]] static float EstimateAverageLuminance(const RenderScene& scene, const SceneRenderLightingConfig& lightingConfig) noexcept;
    [[nodiscard]] static SceneExposureHistogram BuildLightingHistogram(const RenderScene& scene, const SceneRenderLightingConfig& lightingConfig) noexcept;
    [[nodiscard]] static SceneExposureHistogram BuildHdrReadbackHistogram(std::span<const std::uint8_t> rgba8Pixels) noexcept;
    [[nodiscard]] static SceneExposureHistogram BuildGpuHistogramReadbackHistogram(std::span<const std::uint8_t> rgba8Pixels) noexcept;
    [[nodiscard]] static float MeterAverageLuminance(const SceneExposureHistogram& histogram, SceneExposureMeteringDesc desc = {}) noexcept;

    SceneExposureMeter();
    ~SceneExposureMeter();

    SceneExposureMeter(const SceneExposureMeter&) = delete;
    SceneExposureMeter& operator=(const SceneExposureMeter&) = delete;
    SceneExposureMeter(SceneExposureMeter&&) = delete;
    SceneExposureMeter& operator=(SceneExposureMeter&&) = delete;

    [[nodiscard]] bool InitializeGpuResources();
    void ShutdownGpuResources() noexcept;
    [[nodiscard]] bool IsGpuInitialized() const noexcept;
    [[nodiscard]] SceneHdrExposureReadbackResult SubmitHdrReadback(const SceneHdrExposureReadbackDesc& desc) noexcept;
    void Reset() noexcept;
    // Seeds the adapted luminance without going through Reset()'s "no history" instant-snap-on-next-
    // Update() path, so a renderer reinitialization that has nothing to do with actual scene exposure
    // (e.g. switching MSAA sample count) doesn't cause the very next HdrColor-metered readback -- which
    // may itself be from an incomplete/black transitional frame -- to be treated as truth.
    void Prime(float luminance) noexcept;
    [[nodiscard]] float Update(float meteredAverageLuminance, SceneExposureAdaptationDesc desc = {}) noexcept;
    [[nodiscard]] float Update(const RenderScene& scene, const SceneRenderLightingConfig& lightingConfig, SceneExposureAdaptationDesc desc = {}) noexcept;
    [[nodiscard]] float CurrentLuminance() const noexcept;
    [[nodiscard]] bool HasHistory() const noexcept;

private:
    std::unique_ptr<SceneExposureGpuReadback> gpuReadback_;
    float adaptedAverageLuminance_ = 0.18F;
    bool hasHistory_ = false;
};

} // namespace kb::render
