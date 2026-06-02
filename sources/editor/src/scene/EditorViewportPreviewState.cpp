#include "scene/EditorViewportPreviewState.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

constexpr EditorViewportProfile kProfiles[] = {
    EditorViewportProfile{
        .kind = EditorViewportProfileKind::Free,
        .label = "Free",
    },
    EditorViewportProfile{
        .kind = EditorViewportProfileKind::Pc1080p,
        .label = "PC 1080p",
        .width = 1920,
        .height = 1080,
    },
    EditorViewportProfile{
        .kind = EditorViewportProfileKind::Pc1440p,
        .label = "PC 1440p",
        .width = 2560,
        .height = 1440,
    },
    EditorViewportProfile{
        .kind = EditorViewportProfileKind::PhonePortrait,
        .label = "Phone P",
        .width = 390,
        .height = 844,
        .safeArea = EditorViewportSafeArea{ .left = 0, .top = 47, .right = 0, .bottom = 34 },
        .devicePreview = true,
    },
    EditorViewportProfile{
        .kind = EditorViewportProfileKind::PhoneLandscape,
        .label = "Phone L",
        .width = 844,
        .height = 390,
        .safeArea = EditorViewportSafeArea{ .left = 47, .top = 0, .right = 47, .bottom = 21 },
        .devicePreview = true,
    },
    EditorViewportProfile{
        .kind = EditorViewportProfileKind::Custom,
        .label = "Custom",
        .width = 1280,
        .height = 720,
    },
};

[[nodiscard]] const EditorViewportProfile& ProfileByKind(EditorViewportProfileKind kind) noexcept {
    for (const EditorViewportProfile& profile : kProfiles) {
        if (profile.kind == kind) {
            return profile;
        }
    }
    return kProfiles[0];
}

[[nodiscard]] EditorViewportProfileKind NextProfile(EditorViewportProfileKind kind) noexcept {
    switch (kind) {
    case EditorViewportProfileKind::Free:
        return EditorViewportProfileKind::Pc1080p;
    case EditorViewportProfileKind::Pc1080p:
        return EditorViewportProfileKind::Pc1440p;
    case EditorViewportProfileKind::Pc1440p:
        return EditorViewportProfileKind::PhonePortrait;
    case EditorViewportProfileKind::PhonePortrait:
        return EditorViewportProfileKind::PhoneLandscape;
    case EditorViewportProfileKind::PhoneLandscape:
        return EditorViewportProfileKind::Custom;
    case EditorViewportProfileKind::Custom:
        return EditorViewportProfileKind::Free;
    }
    return EditorViewportProfileKind::Free;
}

} // namespace

EditorViewportProfile EditorViewportPreviewState::Profile() const noexcept {
    EditorViewportProfile profile = ProfileByKind(profile_);
    if (profile.kind == EditorViewportProfileKind::Custom) {
        profile.width = customWidth_;
        profile.height = customHeight_;
    }
    return profile;
}

EditorViewportProfileKind EditorViewportPreviewState::ProfileKind() const noexcept {
    return profile_;
}

EditorViewportFitMode EditorViewportPreviewState::FitMode() const noexcept {
    return fitMode_;
}

EditorViewportCameraMode EditorViewportPreviewState::CameraMode() const noexcept {
    return cameraMode_;
}

std::uint32_t EditorViewportPreviewState::CustomWidth() const noexcept {
    return customWidth_;
}

std::uint32_t EditorViewportPreviewState::CustomHeight() const noexcept {
    return customHeight_;
}

void EditorViewportPreviewState::SetProfile(EditorViewportProfileKind kind) noexcept {
    profile_ = kind;
}

void EditorViewportPreviewState::SetFitMode(EditorViewportFitMode mode) noexcept {
    fitMode_ = mode;
}

void EditorViewportPreviewState::SetCameraMode(EditorViewportCameraMode mode) noexcept {
    cameraMode_ = mode;
}

void EditorViewportPreviewState::SetCustomResolution(std::uint32_t width, std::uint32_t height) noexcept {
    customWidth_ = std::clamp(width, 16U, 16384U);
    customHeight_ = std::clamp(height, 16U, 16384U);
}

void EditorViewportPreviewState::CycleProfile() noexcept {
    profile_ = NextProfile(profile_);
}

void EditorViewportPreviewState::CycleFitMode() noexcept {
    switch (fitMode_) {
    case EditorViewportFitMode::Fit:
        fitMode_ = EditorViewportFitMode::OneToOne;
        break;
    case EditorViewportFitMode::OneToOne:
        fitMode_ = EditorViewportFitMode::Fill;
        break;
    case EditorViewportFitMode::Fill:
        fitMode_ = EditorViewportFitMode::Fit;
        break;
    }
}

void EditorViewportPreviewState::CycleCameraMode() noexcept {
    switch (cameraMode_) {
    case EditorViewportCameraMode::EditorCamera:
        cameraMode_ = EditorViewportCameraMode::GameCamera;
        break;
    case EditorViewportCameraMode::GameCamera:
        cameraMode_ = EditorViewportCameraMode::OverrideCamera;
        break;
    case EditorViewportCameraMode::OverrideCamera:
        cameraMode_ = EditorViewportCameraMode::EditorCamera;
        break;
    }
}

std::uint32_t EditorViewportPreviewState::RenderWidthForPanel(std::uint32_t panelWidth) const noexcept {
    const EditorViewportProfile profile = Profile();
    return profile.width == 0U ? panelWidth : profile.width;
}

std::uint32_t EditorViewportPreviewState::RenderHeightForPanel(std::uint32_t panelHeight) const noexcept {
    const EditorViewportProfile profile = Profile();
    return profile.height == 0U ? panelHeight : profile.height;
}

const char* EditorViewportFitModeLabel(EditorViewportFitMode mode) noexcept {
    switch (mode) {
    case EditorViewportFitMode::Fit:
        return "Fit";
    case EditorViewportFitMode::OneToOne:
        return "1:1";
    case EditorViewportFitMode::Fill:
        return "Fill";
    }
    return "Fit";
}

const char* EditorViewportCameraModeLabel(EditorViewportCameraMode mode) noexcept {
    switch (mode) {
    case EditorViewportCameraMode::EditorCamera:
        return "Editor Cam";
    case EditorViewportCameraMode::GameCamera:
        return "Game Cam";
    case EditorViewportCameraMode::OverrideCamera:
        return "Override Cam";
    }
    return "Game Cam";
}

} // namespace kb::editor
