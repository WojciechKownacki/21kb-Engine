#pragma once

#include "kb/render/post/ScenePostProcessSettings.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

namespace kb::editor {

class MaterialPreviewRenderPolicy {
public:
    MaterialPreviewRenderPolicy() = delete;

    [[nodiscard]] static kb::render::SceneRenderLightingConfig NeutralPbrLightingConfig() noexcept {
        kb::render::SceneRenderLightingConfig lighting{};
        lighting.ambientColor = { 0.24F, 0.24F, 0.24F };
        lighting.ambientIntensity = 0.70F;
        lighting.environmentMode = kb::render::SceneRenderEnvironmentMode::Hemisphere;
        lighting.environmentZenithColor = { 0.78F, 0.80F, 0.82F };
        lighting.environmentGroundColor = { 0.18F, 0.18F, 0.18F };
        lighting.environmentDiffuseIntensity = 0.65F;
        lighting.environmentSpecularIntensity = 0.18F;
        lighting.editorPreviewKeyLightEnabled = true;
        lighting.editorPreviewKeyLightDirection = { 0.35F, -0.62F, 0.70F };
        lighting.editorPreviewKeyLightColor = { 1.0F, 0.98F, 0.94F };
        lighting.editorPreviewKeyLightIntensity = 1.35F;
        lighting.shadowsEnabled = false;
        return lighting;
    }

    [[nodiscard]] static kb::render::ScenePostProcessSettings StableExposurePostProcessSettings() noexcept {
        kb::render::ScenePostProcessSettings settings{};
        settings.bloomEnabled = false;
        settings.temporalAntiAliasingEnabled = false;
        settings.temporalJitterEnabled = false;
        settings.autoExposureMetering = kb::render::ScenePostProcessSettings::AutoExposureMeteringMode::Manual;
        settings.outputTransform.exposureStops = 0.0F;
        settings.outputTransform.autoExposure.enabled = false;
        settings.outputTransform.autoExposure.temporalAdaptationEnabled = false;
        return settings;
    }
};

} // namespace kb::editor
