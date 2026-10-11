#pragma once

#include "kb/render/post/SceneDisplayCompositeRenderer.hpp"

#include <cstdint>

namespace kb::render {

struct ScenePostProcessSettings {
    enum class AutoExposureMeteringMode : std::uint8_t {
        HdrColor,
        SceneLighting,
        Manual,
    };

    bool bloomEnabled = true;
    float bloomStrength = 0.05F;
    float bloomThreshold = 1.0F;
    float bloomSoftKnee = 0.5F;
    float bloomRadiusPixels = 1.5F;
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
    // Darkens the image toward its corners in the final composite, as Unity's and Unreal's
    // Vignette do. Intensity 0..1: how dark the corners get.
    bool vignetteEnabled = false;
    float vignetteIntensity = 0.3F;
    // Screen-space ambient occlusion of the indirect light (the renderer's SSAO pass in the
    // screen-space resolve). Intensity scales the occlusion, radius (m) is the sampled hemisphere.
    // Off by default, as a scene without a profile renders today.
    bool ambientOcclusionEnabled = false;
    float ambientOcclusionIntensity = 1.0F;
    float ambientOcclusionRadius = 0.6F;
};

} // namespace kb::render
