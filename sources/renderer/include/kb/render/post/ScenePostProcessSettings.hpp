#pragma once

#include "kb/render/post/SceneFogProfile.hpp"

#include "kb/render/post/SceneDisplayCompositeRenderer.hpp"

#include <cstdint>

namespace kb::render {

struct ScenePostProcessSettings {
    enum class AutoExposureMeteringMode : std::uint8_t {
        HdrColor,
        SceneLighting,
        Manual,
    };
    static constexpr std::uint32_t kMotionBlurSamplesMin = 4U;
    static constexpr std::uint32_t kMotionBlurSamplesMax = 16U;

    bool bloomEnabled = true;
    float bloomStrength = 0.05F;
    float bloomThreshold = 1.0F;
    float bloomSoftKnee = 0.5F;
    float bloomRadiusPixels = 1.5F;
    bool depthOfFieldEnabled = false;
    float depthOfFieldFocusDistance = 10.0F;
    float depthOfFieldFocusRange = 5.0F;
    float depthOfFieldMaxRadiusPixels = 8.0F;
    bool temporalAntiAliasingEnabled = true;
    bool temporalJitterEnabled = true;
    float temporalHistoryBlend = 0.88F;
    bool fxaaEnabled = false;
    bool tonemapEnabled = true;
    AutoExposureMeteringMode autoExposureMetering = AutoExposureMeteringMode::HdrColor;
    SceneDisplayOutputTransform outputTransform{
        .autoExposure = FullscreenTextureAutoExposureSettings{
            .enabled = true,
        },
    };
    // Darkens the image toward its corners in the final composite.
    // Intensity 0..1: how dark the corners get.
    bool vignetteEnabled = false;
    float vignetteIntensity = 0.3F;
    // Screen-space ambient occlusion of the indirect light (the renderer's SSAO pass in the
    // screen-space resolve). Intensity scales the occlusion, radius (m) is the sampled hemisphere.
    // Off by default, as a scene without a profile renders today.
    bool ambientOcclusionEnabled = false;
    float ambientOcclusionIntensity = 1.0F;
    float ambientOcclusionRadius = 0.6F;
    // Screen-space reflections of the lit scene in glossy surfaces. Intensity 0..1 scales the reflected
    // radiance, max distance (m) is how far a reflection ray travels. Off by default, as a scene without
    // a profile renders today; the defaults are those of SceneRenderLightingConfig.
    bool screenSpaceReflectionsEnabled = false;
    float screenSpaceReflectionsIntensity = 1.0F;
    float screenSpaceReflectionsMaxDistance = 25.0F;
    // Camera and object motion blur along the motion vectors. Intensity 0..1 is the fraction of the
    // frame the shutter stays open, samples (4..16) the taps along each pixel's motion. Off by default.
    bool motionBlurEnabled = false;
    float motionBlurIntensity = 0.5F;
    std::uint32_t motionBlurSamples = 8U;
    // Global illumination (the post-process Global Illumination
    // Method): light bounced off the scene, gathered on screen or traced through the voxel grid
    // around the camera, so a closed room is lit by what its lights reach and not by the sky.
    enum class GlobalIlluminationMethod : std::uint8_t {
        Off,
        ScreenSpace,
        Voxel,
    };
    GlobalIlluminationMethod globalIllumination = GlobalIlluminationMethod::Off;
    float globalIlluminationIntensity = 1.0F;
    // Exponential height fog: density at the base height falling off per metre above it,
    // lit to its colour; volumetric fog adds the light each light's Volumetric Scattering scatters
    // in it, with its shadows (light shafts).
    SceneFogProfile fogProfile = SceneFogProfile::Automatic;
    bool fogEnabled = false;
    float fogDensity = 0.02F;
    float fogHeightFalloff = 0.2F;
    float fogBaseHeight = 0.0F;
    float fogColorR = 0.6F;
    float fogColorG = 0.65F;
    float fogColorB = 0.7F;
    bool volumetricFogEnabled = false;
    float fogAnisotropy = 0.3F;
};

} // namespace kb::render
