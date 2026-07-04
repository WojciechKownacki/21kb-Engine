#include "kb/render/post/SceneExposureMeter.hpp"

#include "post/SceneExposureGpuReadback.hpp"
#include "post/SceneExposureHistogramBuilder.hpp"

#include <algorithm>
#include <cmath>

namespace kb::render {
bool SceneHdrExposureReadbackDesc::IsValid() const noexcept {
    return bgfx::isValid(hdrColor) && extent.IsValid();
}

SceneExposureMeter::SceneExposureMeter() = default;

SceneExposureMeter::~SceneExposureMeter() = default;

float SceneExposureMeter::EstimateAverageLuminance(const RenderScene& scene, const SceneRenderLightingConfig& lightingConfig) noexcept {
    return SceneExposureHistogramBuilder::EstimateAverageLuminance(scene, lightingConfig);
}

SceneExposureHistogram SceneExposureMeter::BuildLightingHistogram(const RenderScene& scene, const SceneRenderLightingConfig& lightingConfig) noexcept {
    return SceneExposureHistogramBuilder::BuildLightingHistogram(scene, lightingConfig);
}

SceneExposureHistogram SceneExposureMeter::BuildHdrReadbackHistogram(std::span<const std::uint8_t> rgba8Pixels) noexcept {
    return SceneExposureHistogramBuilder::BuildHdrReadbackHistogram(rgba8Pixels);
}

SceneExposureHistogram SceneExposureMeter::BuildGpuHistogramReadbackHistogram(std::span<const std::uint8_t> rgba8Pixels) noexcept {
    return SceneExposureHistogramBuilder::BuildGpuHistogramReadbackHistogram(rgba8Pixels);
}

float SceneExposureMeter::MeterAverageLuminance(const SceneExposureHistogram& histogram, SceneExposureMeteringDesc desc) noexcept {
    return SceneExposureHistogramBuilder::MeterAverageLuminance(histogram, desc);
}

bool SceneExposureMeter::InitializeGpuResources() {
    if (gpuReadback_ == nullptr) {
        gpuReadback_ = std::make_unique<SceneExposureGpuReadback>();
    }
    return gpuReadback_->Initialize();
}

void SceneExposureMeter::ShutdownGpuResources() noexcept {
    if (gpuReadback_ != nullptr) {
        gpuReadback_->Shutdown();
    }
}

bool SceneExposureMeter::IsGpuInitialized() const noexcept {
    return gpuReadback_ != nullptr && gpuReadback_->IsInitialized();
}

SceneHdrExposureReadbackResult SceneExposureMeter::SubmitHdrReadback(const SceneHdrExposureReadbackDesc& desc) noexcept {
    return gpuReadback_ == nullptr ? SceneHdrExposureReadbackResult{} : gpuReadback_->Submit(desc);
}

void SceneExposureMeter::Reset() noexcept {
    adaptedAverageLuminance_ = 0.18F;
    hasHistory_ = false;
    if (gpuReadback_ != nullptr) {
        gpuReadback_->Reset();
    }
}

void SceneExposureMeter::Prime(float luminance) noexcept {
    adaptedAverageLuminance_ = std::clamp(
        luminance,
        SceneExposureHistogramBuilder::kMinAverageLuminance,
        SceneExposureHistogramBuilder::kMaxAverageLuminance);
    hasHistory_ = true;
}

float SceneExposureMeter::Update(float meteredAverageLuminance, SceneExposureAdaptationDesc desc) noexcept {
    const float target = std::clamp(
        meteredAverageLuminance,
        SceneExposureHistogramBuilder::kMinAverageLuminance,
        SceneExposureHistogramBuilder::kMaxAverageLuminance);
    if (!hasHistory_ || !desc.enabled) {
        adaptedAverageLuminance_ = target;
        hasHistory_ = true;
        return adaptedAverageLuminance_;
    }

    const float currentLog = std::log2(std::clamp(
        adaptedAverageLuminance_,
        SceneExposureHistogramBuilder::kMinAverageLuminance,
        SceneExposureHistogramBuilder::kMaxAverageLuminance));
    const float targetLog = std::log2(target);
    const float rate = targetLog > currentLog ? desc.brightAdaptationRate : desc.darkAdaptationRate;
    const float alpha = 1.0F - std::exp(-std::max(rate, 0.0F) * std::max(desc.deltaSeconds, 0.0F));
    adaptedAverageLuminance_ = std::exp2(currentLog + ((targetLog - currentLog) * std::clamp(alpha, 0.0F, 1.0F)));
    return std::clamp(
        adaptedAverageLuminance_,
        SceneExposureHistogramBuilder::kMinAverageLuminance,
        SceneExposureHistogramBuilder::kMaxAverageLuminance);
}

float SceneExposureMeter::Update(const RenderScene& scene, const SceneRenderLightingConfig& lightingConfig, SceneExposureAdaptationDesc desc) noexcept {
    return Update(EstimateAverageLuminance(scene, lightingConfig), desc);
}

float SceneExposureMeter::CurrentLuminance() const noexcept {
    return adaptedAverageLuminance_;
}

bool SceneExposureMeter::HasHistory() const noexcept {
    return hasHistory_;
}

} // namespace kb::render
