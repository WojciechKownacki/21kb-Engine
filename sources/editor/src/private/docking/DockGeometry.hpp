#pragma once

#include "kb/editor/docking/DockTypes.hpp"

namespace kb::editor {

class DockGeometry {
public:
    DockGeometry() = delete;

    [[nodiscard]] static int ClampInt(int value, int minValue, int maxValue);
    [[nodiscard]] static float ClampRatio(float value);
    [[nodiscard]] static DockRect MakeRect(int x, int y, int width, int height);
    [[nodiscard]] static DockRect Split(const DockRect& rect, DockDropZone zone, float ratio);
    // Where a panel's content sits inside the frame it was given: under the tab
    // strip and out to the frame's hairline. A panel torn off into its own window is
    // the same panel, so it is laid out from here too - anything else leaves it
    // sitting in a margin its docked twin does not have.
    [[nodiscard]] static DockRect PanelContent(const DockRect& frame, int tabStripHeight);
};

} // namespace kb::editor
