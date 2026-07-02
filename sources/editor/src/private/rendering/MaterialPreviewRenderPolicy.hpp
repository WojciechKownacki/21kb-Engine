#pragma once

#include "kb/render/post/ScenePostProcessSettings.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "scene/material_preview/EditorMaterialPreviewSettings.hpp"

namespace kb::editor {

class MaterialPreviewRenderPolicy {
public:
    MaterialPreviewRenderPolicy() = delete;

    [[nodiscard]] static kb::render::SceneRenderLightingPath SceneLightingPath(
        kb::project::ProjectSceneLightingPath projectLightingPath) noexcept {
        switch (projectLightingPath) {
        case kb::project::ProjectSceneLightingPath::Deferred:
            return kb::render::SceneRenderLightingPath::Deferred;
        case kb::project::ProjectSceneLightingPath::Forward:
        default:
            return kb::render::SceneRenderLightingPath::Forward;
        }
    }

    [[nodiscard]] static kb::render::SceneRenderLightingConfig NeutralPbrLightingConfig(
        EditorMaterialPreviewSceneSettings settings = EditorMaterialPreviewSceneSettings::Defaults(),
        kb::project::ProjectSceneLightingPath projectLightingPath = kb::project::ProjectSceneLightingPath::Forward) noexcept {
        kb::render::SceneRenderLightingConfig lighting{};
        lighting.lightingPath = SceneLightingPath(projectLightingPath);
        lighting.ambientColor = { 0.24F, 0.24F, 0.24F };
        lighting.ambientIntensity = settings.ambientIntensity;
        lighting.environmentMode = kb::render::SceneRenderEnvironmentMode::Hemisphere;
        lighting.environmentZenithColor = { 0.78F, 0.80F, 0.82F };
        lighting.environmentGroundColor = { 0.18F, 0.18F, 0.18F };
        lighting.environmentDiffuseIntensity = settings.environmentDiffuseIntensity;
        lighting.environmentSpecularIntensity = settings.environmentSpecularIntensity;
        lighting.editorPreviewKeyLightEnabled = true;
        lighting.editorPreviewKeyLightDirection = { 0.35F, -0.62F, 0.70F };
        lighting.editorPreviewKeyLightColor = { 1.0F, 0.98F, 0.94F };
        lighting.editorPreviewKeyLightIntensity = settings.keyLightIntensity;
        lighting.shadowsEnabled = false;
        return lighting;
    }

    [[nodiscard]] static kb::render::ScenePostProcessSettings StableExposurePostProcessSettings(
        EditorMaterialPreviewSceneSettings previewSettings = EditorMaterialPreviewSceneSettings::Defaults()) noexcept {
        kb::render::ScenePostProcessSettings postProcess{};
        postProcess.bloomEnabled = false;
        postProcess.temporalAntiAliasingEnabled = false;
        postProcess.temporalJitterEnabled = false;
        postProcess.autoExposureMetering = kb::render::ScenePostProcessSettings::AutoExposureMeteringMode::Manual;
        postProcess.outputTransform.exposureStops = previewSettings.exposureStops;
        postProcess.outputTransform.autoExposure.enabled = false;
        postProcess.outputTransform.autoExposure.temporalAdaptationEnabled = false;
        return postProcess;
    }
};

} // namespace kb::editor
