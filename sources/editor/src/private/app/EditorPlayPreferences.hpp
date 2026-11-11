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

// The picture sizes the Game View offers. Free Aspect follows the panel; the others render at their own size
// and are fitted inside the panel with bars at the sides. The names are written to the editor settings.
enum class EditorGameViewResolution : std::uint8_t {
    FreeAspect,
    ProjectResolution,
    Landscape16x9,
    Portrait9x16,
    PhoneLandscape,
    Tablet4x3,
    Tablet16x10,
};

// The places in the order the arrow beside Play lists them; Start Paused follows.
inline constexpr std::array<EditorPlayTarget, 4U> kEditorPlayTargetMenuOrder{
    EditorPlayTarget::GameView,
    EditorPlayTarget::SceneView,
    EditorPlayTarget::SceneViewMaximized,
    EditorPlayTarget::Standalone,
};

struct EditorPlayPreferences {
    EditorPlayTarget target = EditorPlayTarget::GameView;
    EditorGameViewResolution gameViewResolution = EditorGameViewResolution::FreeAspect;
    // Play mode begins paused, so the first frame can be looked at.
    bool startPaused = false;

    [[nodiscard]] bool operator==(const EditorPlayPreferences&) const noexcept = default;
};

} // namespace kb::editor
