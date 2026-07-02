#pragma once

#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <cstdint>
#include <string_view>

namespace kb::editor {

enum class EditorMaterialPreviewLightingPreset : std::uint8_t {
    Studio,
    Neutral,
    HighContrast,
};

struct EditorMaterialPreviewSceneSettings {
    EditorMaterialPreviewLightingPreset lightingPreset = EditorMaterialPreviewLightingPreset::Studio;
    kb::render::RenderMaterialGraphQualityLevel qualityLevel = kb::render::RenderMaterialGraphQualityLevel::High;
    float cameraDistance = 4.0F;
    float verticalFovDegrees = 38.0F;
    float keyLightIntensity = 1.35F;
    float ambientIntensity = 0.70F;
    float environmentDiffuseIntensity = 0.65F;
    float environmentSpecularIntensity = 0.18F;
    float exposureStops = 0.0F;
    bool postProcessEnabled = true;

    [[nodiscard]] static EditorMaterialPreviewSceneSettings Defaults() noexcept {
        return EditorMaterialPreviewSceneSettings{};
    }
};

[[nodiscard]] inline std::string_view EditorMaterialPreviewLightingPresetName(EditorMaterialPreviewLightingPreset preset) noexcept {
    switch (preset) {
    case EditorMaterialPreviewLightingPreset::Studio:
        return "Studio";
    case EditorMaterialPreviewLightingPreset::Neutral:
        return "Neutral";
    case EditorMaterialPreviewLightingPreset::HighContrast:
        return "High Contrast";
    }
    return "Studio";
}

[[nodiscard]] inline std::string_view EditorMaterialPreviewQualityLevelName(kb::render::RenderMaterialGraphQualityLevel qualityLevel) noexcept {
    switch (qualityLevel) {
    case kb::render::RenderMaterialGraphQualityLevel::Low:
        return "Low";
    case kb::render::RenderMaterialGraphQualityLevel::Medium:
        return "Medium";
    case kb::render::RenderMaterialGraphQualityLevel::High:
        return "High";
    case kb::render::RenderMaterialGraphQualityLevel::Epic:
        return "Epic";
    }
    return "High";
}

[[nodiscard]] inline kb::render::RenderMaterialGraphQualityLevel NextEditorMaterialPreviewQualityLevel(
    kb::render::RenderMaterialGraphQualityLevel qualityLevel) noexcept {
    switch (qualityLevel) {
    case kb::render::RenderMaterialGraphQualityLevel::Low:
        return kb::render::RenderMaterialGraphQualityLevel::Medium;
    case kb::render::RenderMaterialGraphQualityLevel::Medium:
        return kb::render::RenderMaterialGraphQualityLevel::High;
    case kb::render::RenderMaterialGraphQualityLevel::High:
        return kb::render::RenderMaterialGraphQualityLevel::Epic;
    case kb::render::RenderMaterialGraphQualityLevel::Epic:
        return kb::render::RenderMaterialGraphQualityLevel::Low;
    }
    return kb::render::RenderMaterialGraphQualityLevel::High;
}

[[nodiscard]] inline EditorMaterialPreviewSceneSettings EditorMaterialPreviewSceneSettingsForPreset(EditorMaterialPreviewLightingPreset preset) noexcept {
    EditorMaterialPreviewSceneSettings settings{};
    settings.lightingPreset = preset;
    switch (preset) {
    case EditorMaterialPreviewLightingPreset::Studio:
        return settings;
    case EditorMaterialPreviewLightingPreset::Neutral:
        settings.keyLightIntensity = 0.95F;
        settings.ambientIntensity = 0.52F;
        settings.environmentDiffuseIntensity = 0.50F;
        settings.environmentSpecularIntensity = 0.10F;
        return settings;
    case EditorMaterialPreviewLightingPreset::HighContrast:
        settings.keyLightIntensity = 1.85F;
        settings.ambientIntensity = 0.22F;
        settings.environmentDiffuseIntensity = 0.30F;
        settings.environmentSpecularIntensity = 0.30F;
        settings.exposureStops = -0.20F;
        return settings;
    }
    return settings;
}

[[nodiscard]] inline EditorMaterialPreviewLightingPreset NextEditorMaterialPreviewLightingPreset(EditorMaterialPreviewLightingPreset preset) noexcept {
    switch (preset) {
    case EditorMaterialPreviewLightingPreset::Studio:
        return EditorMaterialPreviewLightingPreset::Neutral;
    case EditorMaterialPreviewLightingPreset::Neutral:
        return EditorMaterialPreviewLightingPreset::HighContrast;
    case EditorMaterialPreviewLightingPreset::HighContrast:
        return EditorMaterialPreviewLightingPreset::Studio;
    }
    return EditorMaterialPreviewLightingPreset::Studio;
}

} // namespace kb::editor
