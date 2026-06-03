#pragma once

#include "kb/render/frame/RenderTargetDesc.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace kb::render {

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

    [[nodiscard]] bool InitializeGpuResources();
    void ShutdownGpuResources() noexcept;
    [[nodiscard]] bool IsGpuInitialized() const noexcept;
    [[nodiscard]] SceneHdrExposureReadbackResult SubmitHdrReadback(const SceneHdrExposureReadbackDesc& desc) noexcept;
    void Reset() noexcept;
    [[nodiscard]] float Update(float meteredAverageLuminance, SceneExposureAdaptationDesc desc = {}) noexcept;
    [[nodiscard]] float Update(const RenderScene& scene, const SceneRenderLightingConfig& lightingConfig, SceneExposureAdaptationDesc desc = {}) noexcept;
    [[nodiscard]] float CurrentLuminance() const noexcept;
    [[nodiscard]] bool HasHistory() const noexcept;

private:
    void DestroyGpuResources() noexcept;
    [[nodiscard]] bool ConsumeHdrReadback(std::uint32_t completedFrame) noexcept;
    [[nodiscard]] bool CreateHdrReadbackTarget() noexcept;

    bgfx::ProgramHandle hdrLuminanceProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle hdrSourceSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle hdrExposureParams_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout fullscreenLayout_{};
    bgfx::VertexBufferHandle fullscreenVertexBuffer_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle hdrReadbackRenderTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle hdrReadbackTexture_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle hdrReadbackFrameBuffer_ = BGFX_INVALID_HANDLE;
    std::vector<std::uint8_t> hdrReadbackBytes_{};
    std::uint32_t hdrReadbackReadyFrame_ = 0;
    float latestHdrAverageLuminance_ = 0.18F;
    bool hdrReadbackPending_ = false;
    bool latestHdrSampleValid_ = false;
    float adaptedAverageLuminance_ = 0.18F;
    bool hasHistory_ = false;
};

} // namespace kb::render
