#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

// A torn-off panel wears the same chrome as a docked one - its own tab and window
// buttons, drawn by the editor. The window still needs a resizable frame for the
// edges to be draggable, but Windows would then keep a strip of that frame for
// itself above and around what the editor draws, which reads as an empty bar over
// the panel. Handing the whole window back as the client area removes that strip
// while the frame, and with it the resize edges, stays.
class EditorFloatingWindowFrame {
public:
    EditorFloatingWindowFrame() = delete;

    [[nodiscard]] static LRESULT HandleNonClientCalcSize(HWND window, WPARAM wparam, LPARAM lparam);
};

#endif

} // namespace kb::editor
