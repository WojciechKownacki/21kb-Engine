#pragma once

#include "kb/editor/docking/DockTypes.hpp"

namespace kb::editor {

class DockDropZoneClassifier {
public:
    [[nodiscard]] DockDropZone DominantOuterEdge(const DockRect& rect, int x, int y) const noexcept;
    [[nodiscard]] DockDropZone ClassifyLeafZone(const DockRect& rect, int x, int y) const noexcept;

private:
    [[nodiscard]] static DockDropZone DominantZone(bool left, bool right, bool top, bool bottom, int leftDepth, int rightDepth, int topDepth, int bottomDepth) noexcept;
};

} // namespace kb::editor
