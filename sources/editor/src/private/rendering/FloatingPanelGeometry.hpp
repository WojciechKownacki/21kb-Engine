#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

// A panel torn off into its own window is laid out exactly as it is inside a dock
// leaf: its content starts under the tab strip and runs to the window's hairline.
// Rendering, hit testing and the overlays all ask here, so a torn-off panel cannot
// drift away from its docked twin.
class FloatingPanelGeometry {
public:
    FloatingPanelGeometry() = delete;

    [[nodiscard]] static RECT Content(const RECT& client, int tabStripHeight) noexcept;
};

#endif

} // namespace kb::editor
