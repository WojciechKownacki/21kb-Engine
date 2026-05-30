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
};

} // namespace kb::editor
