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

    // Whether the scene's authored screen-space UI belongs in this viewport at all.
    //
    // A canvas is not part of the world. Drawing it over the 3D view puts a second coordinate
    // space on top of the one being edited, which buries the map behind a menu and makes level
    // work impossible. So the UI layer shows in the two places it means something: while
    // playing, and in the 2D authoring mode that exists for it.
    //
    // Both the renderer and the pointer router read this one predicate, because drawing and
    // picking must never disagree: a hidden canvas that still swallowed clicks would be worse
    // than the visible one it replaced.
    [[nodiscard]] static constexpr bool ScreenUIVisible(bool playModeSceneActive, bool twoDAuthoring) noexcept {
        return playModeSceneActive || twoDAuthoring;
    }

    [[nodiscard]] static constexpr bool RequiresPresent(
        bool previousPlayModeSceneActive,
        bool currentPlayModeSceneActive) noexcept {
        return previousPlayModeSceneActive != currentPlayModeSceneActive;
    }
};

} // namespace kb::editor
