#include "kb/render/post/ScenePostProcessVolumeBlend.hpp"

#include "engine/scene/ScenePostProcessVolumes.hpp"

#include <cmath>
#include <vector>

namespace kb::render {
namespace {

// a * (1 - t) + b * t: exactly a at 0 and exactly b at 1.
[[nodiscard]] float Mix(float a, float b, float t) noexcept {
    return a * (1.0F - t) + b * t;
}

template <typename T>
[[nodiscard]] T Pick(T base, T layer, float weight) noexcept {
    return weight > 0.5F ? layer : base;
}

} // namespace

ScenePostProcessSettings BlendScenePostProcessSettings(
    const ScenePostProcessSettings& base, const ScenePostProcessSettings& layer, float weight) noexcept {
    ScenePostProcessSettings out = base;
    const float t = weight < 0.0F ? 0.0F : (weight > 1.0F ? 1.0F : weight);
    const auto number = [t](float& field, float baseValue, float layerValue) noexcept { field = Mix(baseValue, layerValue, t); };
#define KB_BLEND_NUMBER(field) number(out.field, base.field, layer.field)
#define KB_BLEND_CHOICE(field) out.field = Pick(base.field, layer.field, t)
    KB_BLEND_CHOICE(bloomEnabled);
    KB_BLEND_NUMBER(bloomStrength);
    KB_BLEND_NUMBER(bloomThreshold);
    KB_BLEND_NUMBER(bloomSoftKnee);
    KB_BLEND_NUMBER(bloomRadiusPixels);
    KB_BLEND_CHOICE(depthOfFieldEnabled);
    KB_BLEND_NUMBER(depthOfFieldFocusDistance);
    KB_BLEND_NUMBER(depthOfFieldFocusRange);
    KB_BLEND_NUMBER(depthOfFieldMaxRadiusPixels);
    KB_BLEND_CHOICE(temporalAntiAliasingEnabled);
    KB_BLEND_CHOICE(temporalJitterEnabled);
    KB_BLEND_NUMBER(temporalHistoryBlend);
    KB_BLEND_CHOICE(fxaaEnabled);
    KB_BLEND_CHOICE(tonemapEnabled);
    KB_BLEND_CHOICE(autoExposureMetering);
    KB_BLEND_NUMBER(outputTransform.exposureStops);
    KB_BLEND_NUMBER(outputTransform.gamma);
    KB_BLEND_CHOICE(outputTransform.tonemap);
    KB_BLEND_NUMBER(outputTransform.colorGradingLutStrength);
    KB_BLEND_CHOICE(outputTransform.autoExposure.enabled);
    KB_BLEND_NUMBER(outputTransform.autoExposure.meteredAverageLuminance);
    KB_BLEND_NUMBER(outputTransform.autoExposure.middleGray);
    KB_BLEND_NUMBER(outputTransform.autoExposure.minExposureStops);
    KB_BLEND_NUMBER(outputTransform.autoExposure.maxExposureStops);
    KB_BLEND_NUMBER(outputTransform.autoExposure.biasStops);
    KB_BLEND_CHOICE(outputTransform.autoExposure.temporalAdaptationEnabled);
    KB_BLEND_NUMBER(outputTransform.autoExposure.brightAdaptationRate);
    KB_BLEND_NUMBER(outputTransform.autoExposure.darkAdaptationRate);
    KB_BLEND_CHOICE(vignetteEnabled);
    KB_BLEND_NUMBER(vignetteIntensity);
    KB_BLEND_CHOICE(chromaticAberrationEnabled);
    KB_BLEND_NUMBER(chromaticAberrationIntensity);
    KB_BLEND_CHOICE(filmGrainEnabled);
    KB_BLEND_NUMBER(filmGrainIntensity);
    KB_BLEND_NUMBER(filmGrainSize);
    KB_BLEND_CHOICE(ambientOcclusionEnabled);
    KB_BLEND_NUMBER(ambientOcclusionIntensity);
    KB_BLEND_NUMBER(ambientOcclusionRadius);
    KB_BLEND_CHOICE(screenSpaceReflectionsEnabled);
    KB_BLEND_NUMBER(screenSpaceReflectionsIntensity);
    KB_BLEND_NUMBER(screenSpaceReflectionsMaxDistance);
    KB_BLEND_CHOICE(motionBlurEnabled);
    KB_BLEND_NUMBER(motionBlurIntensity);
    out.motionBlurSamples = static_cast<std::uint32_t>(std::lround(Mix(
        static_cast<float>(base.motionBlurSamples), static_cast<float>(layer.motionBlurSamples), t)));
    KB_BLEND_CHOICE(globalIllumination);
    KB_BLEND_NUMBER(globalIlluminationIntensity);
    KB_BLEND_CHOICE(fogProfile);
    KB_BLEND_CHOICE(fogEnabled);
    KB_BLEND_NUMBER(fogDensity);
    KB_BLEND_NUMBER(fogHeightFalloff);
    KB_BLEND_NUMBER(fogBaseHeight);
    KB_BLEND_NUMBER(fogColorR);
    KB_BLEND_NUMBER(fogColorG);
    KB_BLEND_NUMBER(fogColorB);
    KB_BLEND_CHOICE(volumetricFogEnabled);
    KB_BLEND_NUMBER(fogAnisotropy);
#undef KB_BLEND_NUMBER
#undef KB_BLEND_CHOICE
    return out;
}

std::optional<ScenePostProcessSettings> ResolveScenePostProcessVolumes(
    const kb::scene::Scene& scene, const kb::math::DVec3& cameraWorldPosition, const ScenePostProcessSettings& base,
    const ScenePostProcessProfileLoader& load, std::uint64_t& unavailableProfileAssetId) {
    std::vector<kb::scene::ScenePostProcessVolumeInfluence> influences;
    kb::scene::ScenePostProcessVolumeInfluences(scene, cameraWorldPosition, influences);
    if (influences.empty()) {
        return std::nullopt;
    }
    ScenePostProcessSettings result = base;
    for (const kb::scene::ScenePostProcessVolumeInfluence& influence : influences) {
        const std::optional<ScenePostProcessSettings> profile = load(influence.profileAssetId);
        if (!profile.has_value()) {
            unavailableProfileAssetId = influence.profileAssetId;
            return std::nullopt;
        }
        result = BlendScenePostProcessSettings(result, *profile, influence.weight);
    }
    return result;
}

} // namespace kb::render
