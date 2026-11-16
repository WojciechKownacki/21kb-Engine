#include "RendererTestSupport.hpp"

#include "kb/render/post/SceneExposureMeter.hpp"
#include "kb/render/post/ScenePostProcessVolumeBlend.hpp"
#include "kb/render/resources/PostProcessProfileAssetLoader.hpp"
#include "engine/scene/PostProcessVolumeComponent.hpp"
#include "engine/scene/RegionShapeComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePostProcessVolumes.hpp"
#include "engine/scene/SceneRegionShapeComponents.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "renderer/RendererExposureSubmitter.hpp"
#include "renderer/RendererFinalCompositeSubmitter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

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

// A profile with tonemapping off still shows the scene at its exposure and display-encoded;
// only the tone curve goes.
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

// The composite applies chromatic aberration and film grain only when they are on, at their clamped settings.
void ImageEffectsFollowTheProfile() {
    PostProcessOutput output;
    const FullscreenTextureImageEffects off = RendererFinalCompositeSubmitter::ImageEffects(output, 7U);
    Require(off.chromaticAberrationIntensity == 0.0F && off.filmGrainIntensity == 0.0F && off.grainFrame == 0U,
        "The default profile must not add chromatic aberration or film grain");
    output.postProcessSettings.chromaticAberrationIntensity = 0.7F;
    output.postProcessSettings.filmGrainIntensity = 0.6F;
    output.postProcessSettings.filmGrainSize = 2.0F;
    const FullscreenTextureImageEffects disabled = RendererFinalCompositeSubmitter::ImageEffects(output, 7U);
    Require(disabled.chromaticAberrationIntensity == 0.0F && disabled.filmGrainIntensity == 0.0F,
        "Disabled effects must stay off whatever their intensity");
    output.postProcessSettings.chromaticAberrationEnabled = true;
    output.postProcessSettings.filmGrainEnabled = true;
    const FullscreenTextureImageEffects on = RendererFinalCompositeSubmitter::ImageEffects(output, 7U);
    Require(NearlyEqual(on.chromaticAberrationIntensity, 0.7F) && NearlyEqual(on.filmGrainIntensity, 0.6F) &&
            NearlyEqual(on.filmGrainSize, 2.0F) && on.grainFrame == 7U,
        "Enabled effects lost their settings or the frame");
    output.postProcessSettings.chromaticAberrationIntensity = 3.0F;
    output.postProcessSettings.filmGrainIntensity = -1.0F;
    output.postProcessSettings.filmGrainSize = 40.0F;
    const FullscreenTextureImageEffects clamped = RendererFinalCompositeSubmitter::ImageEffects(output, 7U);
    Require(NearlyEqual(clamped.chromaticAberrationIntensity, 1.0F) && clamped.filmGrainIntensity == 0.0F &&
            NearlyEqual(clamped.filmGrainSize, ScenePostProcessSettings::kFilmGrainSizeMax),
        "Chromatic aberration and film grain settings must clamp to their ranges");
}

// Every field of a profile moved off its default, so a blend that forgets a field shows.
[[nodiscard]] ScenePostProcessSettings AllFieldsChanged() {
    ScenePostProcessSettings s;
    s.bloomEnabled = false; s.bloomStrength = 0.45F; s.bloomThreshold = 2.0F; s.bloomSoftKnee = 0.9F; s.bloomRadiusPixels = 3.5F;
    s.depthOfFieldEnabled = true; s.depthOfFieldFocusDistance = 20.0F; s.depthOfFieldFocusRange = 9.0F; s.depthOfFieldMaxRadiusPixels = 12.0F;
    s.temporalAntiAliasingEnabled = false; s.temporalJitterEnabled = false; s.temporalHistoryBlend = 0.5F; s.fxaaEnabled = true;
    s.tonemapEnabled = false; s.autoExposureMetering = ScenePostProcessSettings::AutoExposureMeteringMode::Manual;
    s.outputTransform.exposureStops = 1.5F; s.outputTransform.gamma = 2.0F; s.outputTransform.tonemap = FullscreenTextureTonemapOperator::AgxApprox;
    s.outputTransform.colorGradingLutStrength = 0.5F;
    s.outputTransform.autoExposure.enabled = false; s.outputTransform.autoExposure.meteredAverageLuminance = 0.3F;
    s.outputTransform.autoExposure.middleGray = 0.25F; s.outputTransform.autoExposure.minExposureStops = -4.0F;
    s.outputTransform.autoExposure.maxExposureStops = 4.0F; s.outputTransform.autoExposure.biasStops = 1.0F;
    s.outputTransform.autoExposure.temporalAdaptationEnabled = false; s.outputTransform.autoExposure.brightAdaptationRate = 2.0F;
    s.outputTransform.autoExposure.darkAdaptationRate = 0.5F;
    s.vignetteEnabled = true; s.vignetteIntensity = 0.6F;
    s.chromaticAberrationEnabled = true; s.chromaticAberrationIntensity = 0.7F;
    s.filmGrainEnabled = true; s.filmGrainIntensity = 0.8F; s.filmGrainSize = 3.0F;
    s.ambientOcclusionEnabled = true; s.ambientOcclusionIntensity = 2.0F; s.ambientOcclusionRadius = 1.2F;
    s.screenSpaceReflectionsEnabled = true; s.screenSpaceReflectionsIntensity = 0.4F; s.screenSpaceReflectionsMaxDistance = 50.0F;
    s.motionBlurEnabled = true; s.motionBlurIntensity = 0.9F; s.motionBlurSamples = 16U;
    s.globalIllumination = ScenePostProcessSettings::GlobalIlluminationMethod::Voxel; s.globalIlluminationIntensity = 2.0F;
    s.fogProfile = SceneFogProfile::Mobile; s.fogEnabled = true; s.fogDensity = 0.1F; s.fogHeightFalloff = 0.5F; s.fogBaseHeight = 5.0F;
    s.fogColorR = 0.1F; s.fogColorG = 0.2F; s.fogColorB = 0.3F; s.volumetricFogEnabled = true; s.fogAnisotropy = 0.7F;
    return s;
}

[[nodiscard]] std::vector<std::string> ProfileLines(const ScenePostProcessSettings& settings) {
    std::vector<std::string> lines;
    std::istringstream stream{ PostProcessProfileAssetLoader::Describe(settings) };
    for (std::string line; std::getline(stream, line);) lines.push_back(line);
    return lines;
}

void PostProcessVolumesBlendByPlaceAndPriority() {
    const ScenePostProcessSettings base;
    const ScenePostProcessSettings look = AllFieldsChanged();
    const std::vector<std::string> baseLines = ProfileLines(base);
    const std::vector<std::string> lookLines = ProfileLines(look);
    Require(baseLines.size() == lookLines.size(), "A profile lists the same fields whatever their values");
    for (std::size_t line = 0U; line < baseLines.size(); ++line) {
        Require(baseLines[line] != lookLines[line], ("The blend test profile must change every field: " + baseLines[line]).c_str());
    }
    // The blend is exact at both ends, field for field.
    Require(ProfileLines(BlendScenePostProcessSettings(base, look, 0.0F)) == baseLines, "A weight of 0 must leave every field at the base");
    Require(ProfileLines(BlendScenePostProcessSettings(base, look, 1.0F)) == lookLines, "A weight of 1 must give every field the layer's value");
    const ScenePostProcessSettings halfway = BlendScenePostProcessSettings(base, look, 0.5F);
    Require(NearlyEqual(halfway.bloomStrength, 0.25F) && NearlyEqual(halfway.chromaticAberrationIntensity, 0.45F) &&
            NearlyEqual(halfway.filmGrainIntensity, 0.55F) && NearlyEqual(halfway.filmGrainSize, 2.0F) && halfway.motionBlurSamples == 12U,
        "Numbers, the chromatic aberration and the film grain among them, must meet halfway at a weight of one half");
    Require(!halfway.vignetteEnabled && !halfway.fxaaEnabled && halfway.outputTransform.tonemap == base.outputTransform.tonemap,
        "Switches and choices must stay with the base until the weight passes one half");
    const ScenePostProcessSettings past = BlendScenePostProcessSettings(base, look, 0.51F);
    Require(past.vignetteEnabled && past.fxaaEnabled && past.outputTransform.tonemap == FullscreenTextureTonemapOperator::AgxApprox &&
            past.globalIllumination == ScenePostProcessSettings::GlobalIlluminationMethod::Voxel && past.fogProfile == SceneFogProfile::Mobile,
        "Switches and choices must take the layer's value once the weight passes one half");

    kb::scene::Scene empty;
    Require(!kb::scene::SceneHasPostProcessVolumes(empty), "A scene without volumes must report none");

    kb::scene::Scene scene;
    const auto addVolume = [&scene](float size, kb::scene::PostProcessVolumeComponent volume) {
        const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Volume" });
        scene.Components().RegionShapes().Set(object.Entity(),
            kb::scene::RegionShapeComponent{ .kind = kb::scene::RegionShapeKind::Box, .size = { size, size, size } });
        scene.Components().PostProcessVolumes().Set(object.Entity(), volume);
        return object.Entity();
    };
    ScenePostProcessSettings other = base;
    other.bloomStrength = 0.85F;
    other.vignetteEnabled = true;
    other.vignetteIntensity = 0.9F;
    std::map<std::uint64_t, ScenePostProcessSettings> profiles{ { 1U, look }, { 2U, other } };
    const ScenePostProcessProfileLoader load = [&profiles](std::uint64_t id) -> std::optional<ScenePostProcessSettings> {
        const auto found = profiles.find(id);
        if (found == profiles.end()) return std::nullopt;
        return found->second;
    };
    static_cast<void>(addVolume(10.0F, { .profileAssetId = 1U, .priority = 0, .weight = 1.0F, .blendDistance = 10.0F }));
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(kb::scene::SceneHasPostProcessVolumes(scene), "A scene with a volume must report it");
    std::uint64_t unavailable = 0U;
    const auto resolveAt = [&](double x) { return ResolveScenePostProcessVolumes(scene, kb::math::DVec3{ x, 0.0, 0.0 }, base, load, unavailable); };

    // (a) Outside the volume and its blend zone nothing changes: the caller keeps the global settings untouched.
    Require(!resolveAt(100.0).has_value() && !resolveAt(15.5).has_value(), "Outside the blend zone no volume may apply");
    // (b) Fully inside, the volume's values.
    const std::optional<ScenePostProcessSettings> inside = resolveAt(0.0);
    Require(inside.has_value() && ProfileLines(*inside) == lookLines, "Inside the volume every field must take the volume's value");
    const std::optional<ScenePostProcessSettings> onEdge = resolveAt(5.0);
    Require(onEdge.has_value() && ProfileLines(*onEdge) == lookLines, "On the edge of the volume the weight is still full");
    // (c) Halfway through the blend zone, the midpoint.
    const std::optional<ScenePostProcessSettings> middle = resolveAt(10.0);
    Require(middle.has_value() && NearlyEqual(middle->bloomStrength, 0.25F) && NearlyEqual(middle->filmGrainIntensity, 0.55F) &&
            NearlyEqual(middle->chromaticAberrationIntensity, 0.45F) && !middle->vignetteEnabled,
        "Halfway through the blend zone the numbers must be the midpoint and the switches still the base's");
    const std::optional<ScenePostProcessSettings> nearEdge = resolveAt(5.5);
    Require(nearEdge.has_value() && nearEdge->vignetteEnabled && nearEdge->bloomStrength > 0.4F, "Close to the volume the switches must have changed");
    // (d) Overlapping volumes: the higher priority is applied last, so it wins where both are at full weight.
    const kb::scene::SceneEntity second = addVolume(4.0F, { .profileAssetId = 2U, .priority = 5, .weight = 1.0F, .blendDistance = 0.0F });
    static_cast<void>(scene.Runtime().Update(0.0F));
    const std::optional<ScenePostProcessSettings> both = resolveAt(0.0);
    Require(both.has_value() && ProfileLines(*both) == ProfileLines(other), "The volume with the higher priority must win where they overlap");
    kb::scene::PostProcessVolumeComponent lowered = *scene.Components().PostProcessVolumes().TryGet(second);
    lowered.priority = -5;
    scene.Components().PostProcessVolumes().Set(second, lowered);
    const std::optional<ScenePostProcessSettings> reversed = resolveAt(0.0);
    Require(reversed.has_value() && ProfileLines(*reversed) == lookLines, "Swapping the priorities must swap the winner");
    lowered.priority = 5;
    lowered.weight = 0.5F;
    scene.Components().PostProcessVolumes().Set(second, lowered);
    const std::optional<ScenePostProcessSettings> mixed = resolveAt(0.0);
    Require(mixed.has_value() && NearlyEqual(mixed->bloomStrength, 0.65F),
        "A higher priority volume at half weight must move the result half way from the lower one");
    // The global case: a volume on an object without a region shape reaches everywhere.
    kb::scene::Scene everywhere;
    const kb::scene::SceneObject anywhere = everywhere.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Global" });
    everywhere.Components().PostProcessVolumes().Set(anywhere.Entity(), kb::scene::PostProcessVolumeComponent{ .profileAssetId = 2U, .weight = 1.0F });
    const std::optional<ScenePostProcessSettings> distant = ResolveScenePostProcessVolumes(everywhere, kb::math::DVec3{ 1.0e6, -3.0e5, 42.0 }, base, load, unavailable);
    Require(distant.has_value() && ProfileLines(*distant) == ProfileLines(other), "A volume without a region shape must reach everywhere");
    // A volume whose profile is missing is reported, not skipped.
    everywhere.Components().PostProcessVolumes().Set(anywhere.Entity(), kb::scene::PostProcessVolumeComponent{ .profileAssetId = 9U });
    Require(!ResolveScenePostProcessVolumes(everywhere, kb::math::DVec3{}, base, load, unavailable).has_value() && unavailable == 9U,
        "A volume whose profile cannot load must be reported");
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
    ImageEffectsFollowTheProfile();
    PostProcessVolumesBlendByPlaceAndPriority();
}

} // namespace kb::render::tests
