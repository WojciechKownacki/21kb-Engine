#include "RendererTestSupport.hpp"

#include "kb/render/post/SceneExposureMeter.hpp"
#include "renderer/RendererExposureSubmitter.hpp"
#include "renderer/RendererFinalCompositeSubmitter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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

// Brightness that is not a number, is infinite or is far outside the camera's range moves the exposure to a bound
// at most; it never poisons the adapted luminance, and the resolved stops stay finite.
void ExposureStaysFiniteForNonFiniteInput() {
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    constexpr float kInf = std::numeric_limits<float>::infinity();
    SceneExposureMeter meter;
    const SceneExposureAdaptationDesc adapt{.enabled = true, .deltaSeconds = 0.5F, .brightAdaptationRate = 2.0F, .darkAdaptationRate = 2.0F};
    Require(NearlyEqual(meter.Update(kNaN, adapt), 0.18F), "A first measurement that is not a number must seed middle gray");
    static_cast<void>(meter.Update(2.0F, adapt));
    const float before = meter.CurrentLuminance();
    const float afterNaN = meter.Update(kNaN, adapt);
    Require(std::isfinite(afterNaN) && std::isfinite(meter.CurrentLuminance()) && NearlyEqual(afterNaN, before),
        "A measurement that is not a number must leave the adapted luminance unchanged");
    for (const float extreme : { kInf, -kInf, 0.0F, -3.0F, 1.0e30F, -1.0e30F, 1.0e-30F }) {
        const float value = meter.Update(extreme, adapt);
        Require(std::isfinite(value) && value > 0.0F && std::isfinite(meter.CurrentLuminance()),
            "An extreme measurement must keep the adapted luminance finite and positive");
    }
    const SceneExposureAdaptationDesc broken{.enabled = true, .deltaSeconds = kNaN, .brightAdaptationRate = kNaN, .darkAdaptationRate = kInf};
    Require(std::isfinite(meter.Update(1.0F, broken)) && std::isfinite(meter.CurrentLuminance()),
        "Adaptation settings that are not numbers must not poison the adapted luminance");
    meter.Prime(kNaN);
    Require(NearlyEqual(meter.CurrentLuminance(), 0.18F), "Priming with a value that is not a number must give middle gray");

    FullscreenTextureOutputTransform transform;
    transform.autoExposure.enabled = true;
    for (const float bad : { kNaN, kInf, -kInf }) {
        transform.autoExposure.meteredAverageLuminance = bad;
        transform.autoExposure.biasStops = 0.0F;
        Require(std::isfinite(ResolveFullscreenTextureExposureStops(transform)), "Non-finite metered luminance must resolve to finite stops");
        transform.autoExposure.meteredAverageLuminance = 0.18F;
        transform.autoExposure.biasStops = bad;
        Require(std::isfinite(ResolveFullscreenTextureExposureStops(transform)), "Non-finite bias must resolve to finite stops");
        transform.autoExposure.biasStops = 0.0F;
        transform.autoExposure.middleGray = bad;
        Require(std::isfinite(ResolveFullscreenTextureExposureStops(transform)), "Non-finite middle gray must resolve to finite stops");
        transform.autoExposure.middleGray = 0.18F;
        transform.autoExposure.minExposureStops = bad;
        transform.autoExposure.maxExposureStops = bad;
        Require(std::isfinite(ResolveFullscreenTextureExposureStops(transform)), "Non-finite stop limits must resolve to finite stops");
        transform.autoExposure.minExposureStops = -4.0F;
        transform.autoExposure.maxExposureStops = 4.0F;
        transform.autoExposure.enabled = false;
        transform.exposureStops = bad;
        Require(std::isfinite(ResolveFullscreenTextureExposureStops(transform)), "Non-finite manual exposure must resolve to finite stops");
        transform.autoExposure.enabled = true;
        transform.exposureStops = 0.0F;
    }
}

void ExposureSubmissionUsesElapsedFrameTime() {
    RenderScene scene;
    static_cast<void>(scene.UpsertLight({ .entityId = 1U, .kind = RenderLightKind::Directional,
        .intensity = 12.0F }));
    const auto afterOneSecond = [&scene](std::uint32_t frameCount) {
        SceneExposureMeter meter;
        meter.Prime(0.01F);
        PostProcessOutput output;
        output.postProcessSettings.autoExposureMetering = ScenePostProcessSettings::AutoExposureMeteringMode::SceneLighting;
        output.outputTransform.autoExposure.enabled = true;
        output.outputTransform.autoExposure.temporalAdaptationEnabled = true;
        output.outputTransform.autoExposure.brightAdaptationRate = 1.0F;
        for (std::uint32_t frame = 0U; frame < frameCount; ++frame) {
            static_cast<void>(RendererExposureSubmitter::Submit(
                meter, output, {}, {}, scene, {}, frame, 1.0F / static_cast<float>(frameCount)));
        }
        return meter.CurrentLuminance();
    };
    const float atThirty = afterOneSecond(30U);
    const float atSixty = afterOneSecond(60U);
    Require(atThirty > 0.01F && NearlyEqualTolerance(atThirty, atSixty, 0.0001F),
        "Exposure adaptation over one second must agree at 30 and 60 rendered frames");
}

// A profile with tonemapping off still shows the scene at its exposure and display-encoded, as
// Unity, Unreal and Godot do; only the tone curve goes.
void SuddenBrightnessChangesConvergeWithoutOscillation() {
    SceneExposureMeter meter;
    meter.Prime(0.18F);
    const SceneExposureAdaptationDesc adaptation{.enabled = true, .deltaSeconds = 1.0F / 60.0F,
        .brightAdaptationRate = 3.0F, .darkAdaptationRate = 2.0F};
    for (const float target : {1800.0F, 0.00018F, 18.0F, 0.18F}) {
        float previous = meter.CurrentLuminance();
        const bool increasing = target > previous;
        for (unsigned frame = 0; frame < 300U; ++frame) {
            const float next = meter.Update(target, adaptation);
            Require(std::isfinite(next) && next > 0.0F &&
                (increasing ? next >= previous && next <= target : next <= previous && next >= target),
                "Sudden brightness changes caused exposure overshoot or oscillation");
            previous = next;
        }
    }
    meter.Reset();
    Require(NearlyEqual(meter.Update(18.0F, adaptation), 18.0F),
        "Exposure reset kept the previous brightness history");
}

void TonemapOffKeepsExposureAndDisplayEncoding() {
    PostProcessOutput output;
    output.tonemapEnabled = false;
    output.outputTransform.exposureStops = 1.5F;
    output.outputTransform.gamma = 2.2F;
    output.outputTransform.tonemap = SceneDisplayTonemapOperator::Aces;
    output.outputTransform.autoExposure.enabled = true;
    const SceneDisplayOutputTransform shown = RendererFinalCompositeSubmitter::DisplayTransform(output);
    Require(shown.tonemap == SceneDisplayTonemapOperator::None, "Tonemapping off still applied a tone curve");
    Require(NearlyEqual(shown.gamma, 2.2F) && NearlyEqual(shown.exposureStops, 1.5F) && shown.autoExposure.enabled,
        "Tonemapping off dropped the exposure or the display encoding");
    output.tonemapEnabled = true;
    Require(RendererFinalCompositeSubmitter::DisplayTransform(output).tonemap == SceneDisplayTonemapOperator::Aces,
        "Tonemapping on lost the profile's tone curve");
}

// The composite applies the profile's vignette only when it is on, at its clamped intensity.
void VignetteFollowsTheProfile() {
    PostProcessOutput output;
    Require(RendererFinalCompositeSubmitter::VignetteIntensity(output) == 0.0F, "The default profile must not vignette");
    output.postProcessSettings.vignetteEnabled = true;
    output.postProcessSettings.vignetteIntensity = 0.4F;
    Require(NearlyEqual(RendererFinalCompositeSubmitter::VignetteIntensity(output), 0.4F), "An enabled vignette lost its intensity");
    output.postProcessSettings.vignetteIntensity = 3.0F;
    Require(NearlyEqual(RendererFinalCompositeSubmitter::VignetteIntensity(output), 1.0F), "Vignette intensity must clamp to 1");
    output.postProcessSettings.vignetteEnabled = false;
    Require(RendererFinalCompositeSubmitter::VignetteIntensity(output) == 0.0F, "A disabled vignette must not darken");
}

} // namespace

void RunSceneExposureMeterTests() {
    HdrReadbackHistogramPreservesSampleWeights();
    HdrReadbackHistogramMetersLogAverageLuminance();
    GpuHistogramReadbackUsesOnePixelPerBin();
    GpuHistogramReadbackMetersBinCenters();
    ExposureMeteringRejectsOutlierPercentiles();
    TemporalExposureAdaptationUsesRatesAndHistory();
    ExposureStaysFiniteForNonFiniteInput();
    ExposureSubmissionUsesElapsedFrameTime();
    SuddenBrightnessChangesConvergeWithoutOscillation();
    TonemapOffKeepsExposureAndDisplayEncoding();
    VignetteFollowsTheProfile();
}

} // namespace kb::render::tests
