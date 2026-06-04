#include "post/SceneExposureHistogramBuilder.hpp"

#include <algorithm>
#include <cmath>

namespace kb::render {
namespace {

[[nodiscard]] float LinearLuminance(float r, float g, float b) noexcept {
    return std::max(0.0F, (0.2126F * r) + (0.7152F * g) + (0.0722F * b));
}

[[nodiscard]] float EnvironmentLuminance(const SceneRenderLightingConfig& config) noexcept {
    float luminance = LinearLuminance(config.ambientColor[0], config.ambientColor[1], config.ambientColor[2]) *
        std::max(config.ambientIntensity, 0.0F);

    switch (config.environmentMode) {
    case SceneRenderEnvironmentMode::Disabled:
        break;
    case SceneRenderEnvironmentMode::Constant:
        luminance += LinearLuminance(config.environmentZenithColor[0], config.environmentZenithColor[1], config.environmentZenithColor[2]) *
            std::max(config.environmentDiffuseIntensity, 0.0F) * 0.35F;
        break;
    case SceneRenderEnvironmentMode::Hemisphere: {
        const float zenith = LinearLuminance(config.environmentZenithColor[0], config.environmentZenithColor[1], config.environmentZenithColor[2]);
        const float ground = LinearLuminance(config.environmentGroundColor[0], config.environmentGroundColor[1], config.environmentGroundColor[2]);
        luminance += ((zenith + ground) * 0.5F) * std::max(config.environmentDiffuseIntensity, 0.0F) * 0.35F;
        break;
    }
    case SceneRenderEnvironmentMode::ImageBased:
        luminance += std::max(config.ibl.diffuseIntensity, 0.0F) * std::max(config.environmentDiffuseIntensity, 0.0F) * 0.35F;
        luminance += std::max(config.ibl.specularIntensity, 0.0F) * std::max(config.environmentSpecularIntensity, 0.0F) * 0.15F;
        break;
    }

    return luminance;
}

[[nodiscard]] float LightContribution(const LightRenderProxyDesc& light) noexcept {
    if (!light.visible || light.intensity <= 0.0F) {
        return 0.0F;
    }

    const float colorLuminance = LinearLuminance(light.color[0], light.color[1], light.color[2]);
    switch (light.kind) {
    case RenderLightKind::Directional:
        return colorLuminance * light.intensity * 0.45F;
    case RenderLightKind::Point:
        return colorLuminance * light.intensity * 0.08F;
    case RenderLightKind::Spot:
        return colorLuminance * light.intensity * 0.12F;
    case RenderLightKind::AreaRect:
    case RenderLightKind::AreaDisk:
    case RenderLightKind::Tube:
        return colorLuminance * light.intensity * 0.18F;
    }

    return 0.0F;
}

void AddHistogramSample(SceneExposureHistogram& histogram, float luminance, float weight) noexcept {
    if (luminance <= 0.0F || weight <= 0.0F) {
        return;
    }

    const float logLuminance = std::log2(std::clamp(
        luminance,
        SceneExposureHistogramBuilder::kMinAverageLuminance,
        SceneExposureHistogramBuilder::kMaxAverageLuminance));
    const float range = std::max(histogram.maxLog2Luminance - histogram.minLog2Luminance, 0.001F);
    const float normalized = std::clamp((logLuminance - histogram.minLog2Luminance) / range, 0.0F, 0.999999F);
    const auto bin = static_cast<std::uint32_t>(normalized * static_cast<float>(SceneExposureHistogram::kBinCount));
    histogram.bins[std::min(bin, SceneExposureHistogram::kBinCount - 1U)] += weight;
    histogram.totalWeight += weight;
}

void AddEncodedHdrReadbackSample(SceneExposureHistogram& histogram, std::uint8_t normalizedLogLuminance, std::uint8_t weight) noexcept {
    if (weight == 0U) {
        return;
    }

    const float normalized = (static_cast<float>(normalizedLogLuminance) + 0.5F) / 255.0F;
    const std::uint32_t bin = std::min(
        static_cast<std::uint32_t>(normalized * static_cast<float>(SceneExposureHistogram::kBinCount)),
        SceneExposureHistogram::kBinCount - 1U);
    const float sampleWeight = static_cast<float>(weight) / 255.0F;
    histogram.bins[bin] += sampleWeight;
    histogram.totalWeight += sampleWeight;
}

void AddEncodedGpuHistogramBin(SceneExposureHistogram& histogram, std::uint32_t bin, std::uint8_t weight) noexcept {
    if (weight == 0U || bin >= SceneExposureHistogram::kBinCount) {
        return;
    }

    const float sampleWeight = static_cast<float>(weight) / 255.0F;
    histogram.bins[bin] += sampleWeight;
    histogram.totalWeight += sampleWeight;
}

[[nodiscard]] float BinCenterLuminance(const SceneExposureHistogram& histogram, std::uint32_t bin) noexcept {
    const float range = histogram.maxLog2Luminance - histogram.minLog2Luminance;
    const float binCenter = (static_cast<float>(bin) + 0.5F) / static_cast<float>(SceneExposureHistogram::kBinCount);
    return std::exp2(histogram.minLog2Luminance + (binCenter * range));
}

} // namespace

float SceneExposureHistogramBuilder::EstimateAverageLuminance(const RenderScene& scene, const SceneRenderLightingConfig& lightingConfig) noexcept {
    return MeterAverageLuminance(BuildLightingHistogram(scene, lightingConfig));
}

SceneExposureHistogram SceneExposureHistogramBuilder::BuildLightingHistogram(const RenderScene& scene, const SceneRenderLightingConfig& lightingConfig) noexcept {
    SceneExposureHistogram histogram{};
    AddHistogramSample(histogram, EnvironmentLuminance(lightingConfig), 1.0F);
    for (const auto& [entityId, light] : scene.LightProxies()) {
        static_cast<void>(entityId);
        const float contribution = LightContribution(light.desc);
        AddHistogramSample(histogram, contribution, light.desc.kind == RenderLightKind::Directional ? 1.0F : 0.35F);
    }

    if (histogram.totalWeight <= 0.0F) {
        AddHistogramSample(histogram, kMinAverageLuminance, 1.0F);
    }
    return histogram;
}

SceneExposureHistogram SceneExposureHistogramBuilder::BuildHdrReadbackHistogram(std::span<const std::uint8_t> rgba8Pixels) noexcept {
    SceneExposureHistogram histogram{
        .minLog2Luminance = kHdrReadbackMinLog2Luminance,
        .maxLog2Luminance = kHdrReadbackMaxLog2Luminance,
    };
    const std::size_t pixelCount = rgba8Pixels.size() / SceneExposureMeter::kHdrReadbackBytesPerPixel;
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const std::size_t offset = pixel * SceneExposureMeter::kHdrReadbackBytesPerPixel;
        AddEncodedHdrReadbackSample(histogram, rgba8Pixels[offset], rgba8Pixels[offset + 3U]);
    }

    if (histogram.totalWeight <= 0.0F) {
        AddHistogramSample(histogram, kMinAverageLuminance, 1.0F);
    }
    return histogram;
}

SceneExposureHistogram SceneExposureHistogramBuilder::BuildGpuHistogramReadbackHistogram(std::span<const std::uint8_t> rgba8Pixels) noexcept {
    SceneExposureHistogram histogram{
        .minLog2Luminance = kHdrReadbackMinLog2Luminance,
        .maxLog2Luminance = kHdrReadbackMaxLog2Luminance,
    };
    const std::size_t binCount = std::min<std::size_t>(rgba8Pixels.size() / SceneExposureMeter::kHdrReadbackBytesPerPixel, SceneExposureHistogram::kBinCount);
    for (std::size_t bin = 0; bin < binCount; ++bin) {
        const std::size_t offset = bin * SceneExposureMeter::kHdrReadbackBytesPerPixel;
        AddEncodedGpuHistogramBin(histogram, static_cast<std::uint32_t>(bin), rgba8Pixels[offset + 3U]);
    }

    if (histogram.totalWeight <= 0.0F) {
        AddHistogramSample(histogram, kMinAverageLuminance, 1.0F);
    }
    return histogram;
}

float SceneExposureHistogramBuilder::MeterAverageLuminance(const SceneExposureHistogram& histogram, SceneExposureMeteringDesc desc) noexcept {
    if (histogram.totalWeight <= 0.0F) {
        return kMinAverageLuminance;
    }

    const float low = std::clamp(desc.lowPercentile, 0.0F, 1.0F);
    const float high = std::clamp(desc.highPercentile, low, 1.0F);
    const float lowCutoff = histogram.totalWeight * low;
    const float highCutoff = histogram.totalWeight * high;
    float cumulative = 0.0F;
    float weightedLogSum = 0.0F;
    float acceptedWeight = 0.0F;

    for (std::uint32_t bin = 0; bin < SceneExposureHistogram::kBinCount; ++bin) {
        const float binWeight = histogram.bins[bin];
        if (binWeight <= 0.0F) {
            continue;
        }

        const float binStart = cumulative;
        const float binEnd = cumulative + binWeight;
        cumulative = binEnd;
        const float accepted = std::max(0.0F, std::min(binEnd, highCutoff) - std::max(binStart, lowCutoff));
        if (accepted <= 0.0F) {
            continue;
        }

        weightedLogSum += std::log2(BinCenterLuminance(histogram, bin)) * accepted;
        acceptedWeight += accepted;
    }

    if (acceptedWeight <= 0.0F) {
        return kMinAverageLuminance;
    }
    return std::clamp(std::exp2(weightedLogSum / acceptedWeight), kMinAverageLuminance, kMaxAverageLuminance);
}

} // namespace kb::render
