#pragma once

#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace kb::editor {

enum class EditorMaterialPreviewLightingPreset : std::uint8_t {
    Studio,
    Neutral,
    HighContrast,
};

// Camera orbit limits, mirroring the feel of Unreal's material-preview viewport: the object stays framed
// while the user swings the camera around it and dollies in/out with the wheel.
inline constexpr float kEditorMaterialPreviewMinCameraDistance = 1.4F;
inline constexpr float kEditorMaterialPreviewMaxCameraDistance = 18.0F;
inline constexpr float kEditorMaterialPreviewMaxPitchDegrees = 85.0F;

struct EditorMaterialPreviewSceneSettings {
    EditorMaterialPreviewLightingPreset lightingPreset = EditorMaterialPreviewLightingPreset::Studio;
    kb::render::RenderMaterialGraphQualityLevel qualityLevel = kb::render::RenderMaterialGraphQualityLevel::High;
    float cameraDistance = 4.0F;
    float verticalFovDegrees = 38.0F;
    // Orbit angles around the framed object (degrees). Yaw swings horizontally, pitch vertically; 0/0 looks
    // straight down -Z at the object, matching the historic fixed camera so thumbnails are unchanged.
    float orbitYawDegrees = 0.0F;
    float orbitPitchDegrees = 0.0F;
    float keyLightIntensity = 1.35F;
    float ambientIntensity = 0.70F;
    float environmentDiffuseIntensity = 0.65F;
    float environmentSpecularIntensity = 0.18F;
    float exposureStops = 0.0F;
    bool postProcessEnabled = true;
    bool normalDebugView = false;

    [[nodiscard]] static EditorMaterialPreviewSceneSettings Defaults() noexcept {
        return EditorMaterialPreviewSceneSettings{};
    }

    // The camera eye position for the current orbit, on a sphere of radius cameraDistance around the object
    // at the origin. Single source of truth so every camera build (preview panel, thumbnail capture) agrees.
    [[nodiscard]] std::array<float, 3U> CameraEye() const noexcept {
        constexpr float degToRad = 3.14159265358979323846F / 180.0F;
        const float yaw = orbitYawDegrees * degToRad;
        const float pitch = std::clamp(orbitPitchDegrees, -kEditorMaterialPreviewMaxPitchDegrees, kEditorMaterialPreviewMaxPitchDegrees) * degToRad;
        const float cosPitch = std::cos(pitch);
        return {
            cameraDistance * cosPitch * std::sin(yaw),
            cameraDistance * std::sin(pitch),
            -cameraDistance * cosPitch * std::cos(yaw),
        };
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
