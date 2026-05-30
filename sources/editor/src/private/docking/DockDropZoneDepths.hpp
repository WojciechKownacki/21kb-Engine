#pragma once

#include "kb/editor/docking/DockTypes.hpp"

namespace kb::editor {

struct DockDropZoneDepths {
    bool left = false;
    bool right = false;
    bool top = false;
    bool bottom = false;
    int leftDepth = -1;
    int rightDepth = -1;
    int topDepth = -1;
    int bottomDepth = -1;
};

class DockDropZoneDepthSelector {
public:
    DockDropZoneDepthSelector() = delete;

    [[nodiscard]] static DockDropZone Select(const DockDropZoneDepths& depths) noexcept;
};

} // namespace kb::editor
