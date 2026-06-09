#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kb::editor {

enum class EditorViewportProfileKind : std::uint8_t {
    Free,
    Pc1080p,
    Pc1440p,
    PhoneLandscape,
    Custom,
};

enum class EditorViewportFitMode : std::uint8_t {
    Fit,
    OneToOne,
    Fill,
};

enum class EditorViewportCameraMode : std::uint8_t {
    EditorCamera,
    GameCamera,
    OverrideCamera,
};

enum class EditorViewportRenderProfile : std::uint8_t {
    Interactive,
    Lit,
    GamePreview,
};

enum class EditorViewportToolbarDropdown : std::uint8_t {
    None,
    GridSpacing,
    SnapStep,
};

struct EditorViewportSafeArea {
    std::uint32_t left = 0;
    std::uint32_t top = 0;
    std::uint32_t right = 0;
    std::uint32_t bottom = 0;
};

struct EditorViewportProfile {
    EditorViewportProfileKind kind = EditorViewportProfileKind::Free;
    std::string_view label = "Free";
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    EditorViewportSafeArea safeArea{};
    bool devicePreview = false;
};

class EditorViewportPreviewState {
public:
    [[nodiscard]] EditorViewportProfile Profile() const noexcept;
    [[nodiscard]] EditorViewportProfileKind ProfileKind() const noexcept;
    [[nodiscard]] EditorViewportFitMode FitMode() const noexcept;
    [[nodiscard]] EditorViewportCameraMode CameraMode() const noexcept;
    [[nodiscard]] EditorViewportRenderProfile RenderProfile() const noexcept;
    [[nodiscard]] std::uint32_t CustomWidth() const noexcept;
    [[nodiscard]] std::uint32_t CustomHeight() const noexcept;
    [[nodiscard]] bool GridVisible() const noexcept;
    [[nodiscard]] float GridSpacing() const noexcept;
    [[nodiscard]] std::uint32_t GridMajorEvery() const noexcept;
    [[nodiscard]] bool SnapEnabled() const noexcept;
    [[nodiscard]] float SnapStep() const noexcept;
    [[nodiscard]] EditorViewportToolbarDropdown ToolbarDropdown() const noexcept;

    void SetProfile(EditorViewportProfileKind kind) noexcept;
    void SetFitMode(EditorViewportFitMode mode) noexcept;
    void SetCameraMode(EditorViewportCameraMode mode) noexcept;
    void SetRenderProfile(EditorViewportRenderProfile profile) noexcept;
    void SetCustomResolution(std::uint32_t width, std::uint32_t height) noexcept;
    void SetGridVisible(bool visible) noexcept;
    void SetGridSpacing(float spacing) noexcept;
    void SetSnapEnabled(bool enabled) noexcept;
    void SetSnapStep(float step) noexcept;
    void CycleProfile() noexcept;
    void CycleFitMode() noexcept;
    void CycleCameraMode() noexcept;
    void CycleRenderProfile() noexcept;
    void ToggleGridVisible() noexcept;
    void ToggleSnapEnabled() noexcept;
    void CycleGridSpacing() noexcept;
    void CycleSnapStep() noexcept;
    void OpenToolbarDropdown(EditorViewportToolbarDropdown dropdown) noexcept;
    void ToggleToolbarDropdown(EditorViewportToolbarDropdown dropdown) noexcept;
    void CloseToolbarDropdown() noexcept;

    [[nodiscard]] kb::scene::Vec3 SnapPosition(kb::scene::Vec3 position) const noexcept;
    [[nodiscard]] kb::scene::Vec3 SnapGroundPosition(kb::scene::Vec3 position) const noexcept;
    [[nodiscard]] kb::scene::Vec3 SnapPositionAxis(kb::scene::Vec3 position, int axis) const noexcept;

    [[nodiscard]] std::uint32_t RenderWidthForPanel(std::uint32_t panelWidth) const noexcept;
    [[nodiscard]] std::uint32_t RenderHeightForPanel(std::uint32_t panelHeight) const noexcept;

private:
    EditorViewportProfileKind profile_ = EditorViewportProfileKind::Free;
    EditorViewportFitMode fitMode_ = EditorViewportFitMode::Fit;
    EditorViewportCameraMode cameraMode_ = EditorViewportCameraMode::GameCamera;
    EditorViewportRenderProfile renderProfile_ = EditorViewportRenderProfile::Interactive;
    std::uint32_t customWidth_ = 1280;
    std::uint32_t customHeight_ = 720;
    bool gridVisible_ = true;
    float gridSpacing_ = 1.0F;
    std::uint32_t gridMajorEvery_ = 10U;
    bool snapEnabled_ = false;
    float snapStep_ = 1.0F;
    EditorViewportToolbarDropdown toolbarDropdown_ = EditorViewportToolbarDropdown::None;
};

[[nodiscard]] const char* EditorViewportFitModeLabel(EditorViewportFitMode mode) noexcept;
[[nodiscard]] const char* EditorViewportCameraModeLabel(EditorViewportCameraMode mode) noexcept;
[[nodiscard]] const char* EditorViewportRenderProfileLabel(EditorViewportRenderProfile profile) noexcept;
[[nodiscard]] const char* EditorViewportGridSpacingLabel(float spacing) noexcept;
[[nodiscard]] const char* EditorViewportSnapStepLabel(float step) noexcept;
[[nodiscard]] std::size_t EditorViewportGridSpacingOptionCount() noexcept;
[[nodiscard]] float EditorViewportGridSpacingOption(std::size_t index) noexcept;
[[nodiscard]] std::size_t EditorViewportSnapStepOptionCount() noexcept;
[[nodiscard]] float EditorViewportSnapStepOption(std::size_t index) noexcept;

} // namespace kb::editor
