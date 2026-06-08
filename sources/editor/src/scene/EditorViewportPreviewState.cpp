#include "scene/EditorViewportPreviewState.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace kb::editor {
namespace {

constexpr std::array<float, 6U> kGridSpacings{ 0.25F, 0.5F, 1.0F, 2.0F, 5.0F, 10.0F };
constexpr std::array<float, 6U> kSnapSteps{ 0.25F, 0.5F, 1.0F, 2.0F, 5.0F, 10.0F };

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

[[nodiscard]] bool NearlyEqual(float a, float b) noexcept {
    return std::fabs(a - b) <= 0.001F;
}

[[nodiscard]] float ClampStep(float value) noexcept {
    if (!std::isfinite(value)) {
        return 1.0F;
    }
    return std::clamp(value, 0.01F, 1000.0F);
}

[[nodiscard]] float CycleFloat(std::span<const float> values, float current) noexcept {
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (NearlyEqual(values[index], current)) {
            return values[(index + 1U) % values.size()];
        }
    }
    return values.empty() ? current : values.front();
}

[[nodiscard]] float SnapValue(float value, float step) noexcept {
    const float safeStep = ClampStep(step);
    return std::round(value / safeStep) * safeStep;
}

[[nodiscard]] const char* StepLabel(std::span<const float> values, std::span<const char* const> labels, float value) noexcept {
    for (std::size_t index = 0; index < values.size() && index < labels.size(); ++index) {
        if (NearlyEqual(values[index], value)) {
            return labels[index];
        }
    }
    return "Custom";
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

bool EditorViewportPreviewState::GridVisible() const noexcept {
    return gridVisible_;
}

float EditorViewportPreviewState::GridSpacing() const noexcept {
    return gridSpacing_;
}

std::uint32_t EditorViewportPreviewState::GridMajorEvery() const noexcept {
    return gridMajorEvery_;
}

bool EditorViewportPreviewState::SnapEnabled() const noexcept {
    return snapEnabled_;
}

float EditorViewportPreviewState::SnapStep() const noexcept {
    return snapStep_;
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

void EditorViewportPreviewState::SetGridVisible(bool visible) noexcept {
    gridVisible_ = visible;
}

void EditorViewportPreviewState::SetGridSpacing(float spacing) noexcept {
    gridSpacing_ = ClampStep(spacing);
}

void EditorViewportPreviewState::SetSnapEnabled(bool enabled) noexcept {
    snapEnabled_ = enabled;
}

void EditorViewportPreviewState::SetSnapStep(float step) noexcept {
    snapStep_ = ClampStep(step);
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

void EditorViewportPreviewState::ToggleGridVisible() noexcept {
    gridVisible_ = !gridVisible_;
}

void EditorViewportPreviewState::ToggleSnapEnabled() noexcept {
    snapEnabled_ = !snapEnabled_;
}

void EditorViewportPreviewState::CycleGridSpacing() noexcept {
    gridSpacing_ = CycleFloat(kGridSpacings, gridSpacing_);
}

void EditorViewportPreviewState::CycleSnapStep() noexcept {
    snapStep_ = CycleFloat(kSnapSteps, snapStep_);
}

kb::scene::Vec3 EditorViewportPreviewState::SnapPosition(kb::scene::Vec3 position) const noexcept {
    if (!snapEnabled_) {
        return position;
    }
    position.x = SnapValue(position.x, snapStep_);
    position.y = SnapValue(position.y, snapStep_);
    position.z = SnapValue(position.z, snapStep_);
    return position;
}

kb::scene::Vec3 EditorViewportPreviewState::SnapGroundPosition(kb::scene::Vec3 position) const noexcept {
    if (!snapEnabled_) {
        return position;
    }
    position.x = SnapValue(position.x, snapStep_);
    position.z = SnapValue(position.z, snapStep_);
    return position;
}

kb::scene::Vec3 EditorViewportPreviewState::SnapPositionAxis(kb::scene::Vec3 position, int axis) const noexcept {
    if (!snapEnabled_) {
        return position;
    }
    switch (axis) {
    case 0:
        position.x = SnapValue(position.x, snapStep_);
        break;
    case 1:
        position.y = SnapValue(position.y, snapStep_);
        break;
    case 2:
        position.z = SnapValue(position.z, snapStep_);
        break;
    default:
        return SnapPosition(position);
    }
    return position;
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

const char* EditorViewportGridSpacingLabel(float spacing) noexcept {
    constexpr std::array<const char*, 6U> labels{ "0.25m", "0.5m", "1m", "2m", "5m", "10m" };
    return StepLabel(kGridSpacings, labels, spacing);
}

const char* EditorViewportSnapStepLabel(float step) noexcept {
    constexpr std::array<const char*, 6U> labels{ "0.25m", "0.5m", "1m", "2m", "5m", "10m" };
    return StepLabel(kSnapSteps, labels, step);
}

} // namespace kb::editor
