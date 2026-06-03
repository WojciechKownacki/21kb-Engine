#include "RendererTestSupport.hpp"

#include "kb/render/post/SceneExposureMeter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace kb::render::tests {
namespace {

[[nodiscard]] bool NearlyEqualTolerance(float lhs, float rhs, float tolerance) noexcept {
    return std::fabs(lhs - rhs) <= tolerance;
}

void HdrReadbackHistogramPreservesSampleWeights() {
    constexpr std::array<std::uint8_t, 12U> pixels{
        96U, 0U, 0U, 255U,
        96U, 0U, 0U, 128U,
        96U, 0U, 0U, 0U,
    };

    const SceneExposureHistogram histogram = SceneExposureMeter::BuildHdrReadbackHistogram(pixels);
    Require(NearlyEqualTolerance(histogram.totalWeight, 1.5019608F, 0.0001F), "HDR readback histogram did not preserve encoded sample weights");

    const float populatedWeight = std::accumulate(histogram.bins.begin(), histogram.bins.end(), 0.0F);
    Require(NearlyEqualTolerance(populatedWeight, histogram.totalWeight, 0.0001F), "HDR readback histogram bin weights do not match total weight");
}

void HdrReadbackHistogramMetersLogAverageLuminance() {
    std::array<std::uint8_t, SceneExposureMeter::kHdrReadbackByteCount> pixels{};
    for (std::uint32_t pixel = 0; pixel < SceneExposureMeter::kHdrReadbackPixelCount; ++pixel) {
        const std::uint32_t offset = pixel * SceneExposureMeter::kHdrReadbackBytesPerPixel;
        pixels[offset] = 87U;
        pixels[offset + 3U] = 255U;
    }

    const SceneExposureHistogram histogram = SceneExposureMeter::BuildHdrReadbackHistogram(pixels);
    const float luminance = SceneExposureMeter::MeterAverageLuminance(histogram, SceneExposureMeteringDesc{
        .lowPercentile = 0.0F,
        .highPercentile = 1.0F,
    });

    Require(luminance > 0.15F && luminance < 0.35F, "HDR readback histogram did not meter expected middle-gray luminance");
}

void GpuHistogramReadbackUsesOnePixelPerBin() {
    std::array<std::uint8_t, SceneExposureMeter::kGpuHistogramReadbackByteCount> pixels{};
    pixels[(27U * SceneExposureMeter::kHdrReadbackBytesPerPixel) + 3U] = 255U;
    pixels[(28U * SceneExposureMeter::kHdrReadbackBytesPerPixel) + 3U] = 128U;
    pixels[(63U * SceneExposureMeter::kHdrReadbackBytesPerPixel) + 3U] = 0U;

    const SceneExposureHistogram histogram = SceneExposureMeter::BuildGpuHistogramReadbackHistogram(pixels);
    Require(NearlyEqualTolerance(histogram.bins[27], 1.0F, 0.0001F), "GPU exposure histogram did not decode full bin weight");
    Require(NearlyEqualTolerance(histogram.bins[28], 128.0F / 255.0F, 0.0001F), "GPU exposure histogram did not decode partial bin weight");
    Require(NearlyEqualTolerance(histogram.bins[63], 0.0F, 0.0001F), "GPU exposure histogram counted an empty bin");
    Require(NearlyEqualTolerance(histogram.totalWeight, 1.0F + (128.0F / 255.0F), 0.0001F), "GPU exposure histogram total weight is wrong");
}

void GpuHistogramReadbackMetersBinCenters() {
    std::array<std::uint8_t, SceneExposureMeter::kGpuHistogramReadbackByteCount> pixels{};
    pixels[(27U * SceneExposureMeter::kHdrReadbackBytesPerPixel) + 3U] = 255U;
    pixels[(28U * SceneExposureMeter::kHdrReadbackBytesPerPixel) + 3U] = 255U;

    const SceneExposureHistogram histogram = SceneExposureMeter::BuildGpuHistogramReadbackHistogram(pixels);
    const float luminance = SceneExposureMeter::MeterAverageLuminance(histogram, SceneExposureMeteringDesc{
        .lowPercentile = 0.0F,
        .highPercentile = 1.0F,
    });
    Require(luminance > 1.0F && luminance < 1.6F, "GPU exposure histogram did not meter expected mid-bin luminance");
}

void ExposureMeteringRejectsOutlierPercentiles() {
    SceneExposureHistogram histogram{
        .minLog2Luminance = -8.0F,
        .maxLog2Luminance = 8.0F,
    };
    histogram.bins[0] = 20.0F;
    histogram.bins[32] = 60.0F;
    histogram.bins[63] = 20.0F;
    histogram.totalWeight = 100.0F;

    const float metered = SceneExposureMeter::MeterAverageLuminance(histogram, SceneExposureMeteringDesc{
        .lowPercentile = 0.2F,
        .highPercentile = 0.8F,
    });
    Require(metered > 0.9F && metered < 1.3F, "Exposure meter did not reject clipped low/high percentile outliers");
}

void TemporalExposureAdaptationUsesRatesAndHistory() {
    SceneExposureMeter meter;
    Require(NearlyEqual(meter.Update(0.18F, SceneExposureAdaptationDesc{.enabled = true, .deltaSeconds = 1.0F}), 0.18F), "Exposure meter did not seed history from first sample");
    Require(meter.HasHistory(), "Exposure meter did not mark history after first sample");

    const float adaptedBright = meter.Update(18.0F, SceneExposureAdaptationDesc{
        .enabled = true,
        .deltaSeconds = 0.25F,
        .brightAdaptationRate = 1.0F,
        .darkAdaptationRate = 1.0F,
    });
    Require(adaptedBright > 0.18F && adaptedBright < 18.0F, "Exposure meter temporal adaptation jumped directly to bright target");

    const float adaptedDark = meter.Update(0.018F, SceneExposureAdaptationDesc{
        .enabled = true,
        .deltaSeconds = 0.25F,
        .brightAdaptationRate = 1.0F,
        .darkAdaptationRate = 4.0F,
    });
    Require(adaptedDark > 0.018F && adaptedDark < adaptedBright, "Exposure meter temporal adaptation did not move toward dark target");

    meter.Reset();
    Require(!meter.HasHistory(), "Exposure meter Reset did not clear temporal history");
    Require(NearlyEqual(meter.CurrentLuminance(), 0.18F), "Exposure meter Reset did not restore middle-gray luminance");
}

} // namespace

void RunSceneExposureMeterTests() {
    HdrReadbackHistogramPreservesSampleWeights();
    HdrReadbackHistogramMetersLogAverageLuminance();
    GpuHistogramReadbackUsesOnePixelPerBin();
    GpuHistogramReadbackMetersBinCenters();
    ExposureMeteringRejectsOutlierPercentiles();
    TemporalExposureAdaptationUsesRatesAndHistory();
}

} // namespace kb::render::tests
