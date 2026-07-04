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
};

} // namespace kb::render
