#pragma once

#include "kb/editor/docking/DockTypes.hpp"

namespace kb::editor {

class DockDropZoneClassifier {
public:
    [[nodiscard]] DockDropZone DominantOuterEdge(const DockRect& rect, int x, int y) const noexcept;
    [[nodiscard]] DockDropZone ClassifyLeafZone(const DockRect& rect, int x, int y) const noexcept;
};

} // namespace kb::editor
