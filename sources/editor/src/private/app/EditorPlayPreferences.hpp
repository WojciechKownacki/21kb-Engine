#pragma once

#include <array>
#include <cstdint>

namespace kb::editor {

// Where Play runs the game. The numbers are written to the editor settings, so a new place goes last.
enum class EditorPlayTarget : std::uint8_t {
    SceneView,
    SceneViewMaximized,
    Standalone,
    GameView,
};

// The device the Game View shows the game on. Each has a screen of its own size and shape; a tablet and a
// phone can also lie or stand. The names are written to the editor settings.
enum class EditorGameViewDevice : std::uint8_t {
    Computer,
    Tablet,
    Phone,
};

struct EditorPlayPreferences {
    EditorPlayTarget target = EditorPlayTarget::GameView;
    EditorGameViewDevice gameViewDevice = EditorGameViewDevice::Computer;
    // A tablet or a phone stands instead of lying.
    bool gameViewPortrait = false;
    // The Game View outlines the part of the picture free of notches, rounded corners and overscan.
    bool gameViewSafeArea = false;
    // Play mode begins paused, so the first frame can be looked at.
    bool startPaused = false;

    [[nodiscard]] bool operator==(const EditorPlayPreferences&) const noexcept = default;
};

} // namespace kb::editor
