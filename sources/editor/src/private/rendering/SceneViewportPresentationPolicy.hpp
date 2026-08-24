#pragma once

namespace kb::editor {

enum class SceneViewportCameraSource {
    Editor,
    PrimaryScene,
};

struct SceneViewportPresentationPolicy {
    [[nodiscard]] static constexpr SceneViewportCameraSource CameraSource(
        bool playModeSceneActive,
        bool primarySceneCameraAvailable) noexcept {
        return playModeSceneActive && primarySceneCameraAvailable
            ? SceneViewportCameraSource::PrimaryScene
            : SceneViewportCameraSource::Editor;
    }

    [[nodiscard]] static constexpr bool EditorOverlaysEnabled(
        bool playModeSceneActive,
        bool primarySceneCameraAvailable) noexcept {
        return CameraSource(playModeSceneActive, primarySceneCameraAvailable) ==
            SceneViewportCameraSource::Editor;
    }

    [[nodiscard]] static constexpr bool RequiresPresent(
        bool previousPlayModeSceneActive,
        bool currentPlayModeSceneActive) noexcept {
        return previousPlayModeSceneActive != currentPlayModeSceneActive;
    }
};

} // namespace kb::editor
