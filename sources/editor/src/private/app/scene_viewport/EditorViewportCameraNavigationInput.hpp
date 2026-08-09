#pragma once

#include "scene/EditorViewportCameraState.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

[[nodiscard]] inline constexpr EditorViewportCameraNavigationMode ResolveEditorViewportCameraNavigationMode(
    bool left,
    bool right,
    bool middle,
    bool alt) noexcept {
    if (middle) return EditorViewportCameraNavigationMode::Pan;
    if (left && right) return EditorViewportCameraNavigationMode::Track;
    if (left) return alt ? EditorViewportCameraNavigationMode::Orbit : EditorViewportCameraNavigationMode::LeftYawDolly;
    if (right) return alt ? EditorViewportCameraNavigationMode::Dolly : EditorViewportCameraNavigationMode::Look;
    return EditorViewportCameraNavigationMode::None;
}

#if defined(_WIN32)
[[nodiscard]] inline bool& EditorViewportNavigationCursorHiddenState() noexcept {
    static bool hidden = false;
    return hidden;
}

[[nodiscard]] inline EditorViewportCameraFlightInput ReadEditorViewportCameraFlightInput() noexcept {
    const auto down = [](int virtualKey) { return (GetAsyncKeyState(virtualKey) & 0x8000) != 0; };
    return EditorViewportCameraFlightInput{
        .forward = down('W'),
        .backward = down('S'),
        .right = down('D'),
        .left = down('A'),
        .up = down('E'),
        .down = down('Q'),
        .boost = down(VK_SHIFT),
        .slow = down(VK_CONTROL),
    };
}

inline void HideEditorViewportNavigationCursor() noexcept {
    bool& hidden = EditorViewportNavigationCursorHiddenState();
    if (hidden) return;
    while (ShowCursor(FALSE) >= 0) {
    }
    hidden = true;
}

inline void RestoreEditorViewportNavigationCursor() noexcept {
    bool& hidden = EditorViewportNavigationCursorHiddenState();
    if (!hidden) return;
    // ShowCursor owns a process-wide display count. Balancing until visible mirrors the hide path
    // and also recovers correctly if capture is released by a different editor viewport.
    while (ShowCursor(TRUE) < 0) {
    }
    hidden = false;
}
#endif

} // namespace kb::editor
