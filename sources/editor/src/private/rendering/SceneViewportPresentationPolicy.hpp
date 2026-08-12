#pragma once

namespace kb::editor {

enum class SceneViewportCameraSource {
    Editor,
    PrimaryScene,
};

struct SceneViewportPresentationPolicy {
    [[nodiscard]] static constexpr SceneViewportCameraSource CameraSource(bool playModeSceneActive) noexcept {
        return playModeSceneActive ? SceneViewportCameraSource::PrimaryScene : SceneViewportCameraSource::Editor;
    }

    [[nodiscard]] static constexpr bool EditorOverlaysEnabled(bool playModeSceneActive) noexcept {
        return !playModeSceneActive;
    }

    [[nodiscard]] static constexpr bool RequiresPresent(
        bool previousPlayModeSceneActive,
        bool currentPlayModeSceneActive) noexcept {
        return previousPlayModeSceneActive != currentPlayModeSceneActive;
    }
};

} // namespace kb::editor
