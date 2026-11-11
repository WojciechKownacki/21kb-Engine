#pragma once

#include <cstdint>

namespace kb::editor {

// Where Play runs the game.
enum class EditorPlayTarget : std::uint8_t {
    SceneView,
    SceneViewMaximized,
    Standalone,
};

struct EditorPlayPreferences {
    EditorPlayTarget target = EditorPlayTarget::SceneView;
    // Play mode begins paused, so the first frame can be looked at.
    bool startPaused = false;

    [[nodiscard]] bool operator==(const EditorPlayPreferences&) const noexcept = default;
};

} // namespace kb::editor
