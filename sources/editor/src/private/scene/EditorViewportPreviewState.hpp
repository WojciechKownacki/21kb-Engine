#pragma once

#include <cstdint>
#include <string_view>

namespace kb::editor {

enum class EditorViewportProfileKind : std::uint8_t {
    Free,
    Pc1080p,
    Pc1440p,
    PhonePortrait,
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
    [[nodiscard]] std::uint32_t CustomWidth() const noexcept;
    [[nodiscard]] std::uint32_t CustomHeight() const noexcept;

    void SetProfile(EditorViewportProfileKind kind) noexcept;
    void SetFitMode(EditorViewportFitMode mode) noexcept;
    void SetCameraMode(EditorViewportCameraMode mode) noexcept;
    void SetCustomResolution(std::uint32_t width, std::uint32_t height) noexcept;
    void CycleProfile() noexcept;
    void CycleFitMode() noexcept;
    void CycleCameraMode() noexcept;

    [[nodiscard]] std::uint32_t RenderWidthForPanel(std::uint32_t panelWidth) const noexcept;
    [[nodiscard]] std::uint32_t RenderHeightForPanel(std::uint32_t panelHeight) const noexcept;

private:
    EditorViewportProfileKind profile_ = EditorViewportProfileKind::Free;
    EditorViewportFitMode fitMode_ = EditorViewportFitMode::Fit;
    EditorViewportCameraMode cameraMode_ = EditorViewportCameraMode::GameCamera;
    std::uint32_t customWidth_ = 1280;
    std::uint32_t customHeight_ = 720;
};

[[nodiscard]] const char* EditorViewportFitModeLabel(EditorViewportFitMode mode) noexcept;
[[nodiscard]] const char* EditorViewportCameraModeLabel(EditorViewportCameraMode mode) noexcept;

} // namespace kb::editor
