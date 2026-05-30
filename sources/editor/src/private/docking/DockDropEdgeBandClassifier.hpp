#pragma once

#include "docking/DockDropZoneDepths.hpp"
#include "kb/editor/docking/DockTypes.hpp"

namespace kb::editor {

class DockDropEdgeBandClassifier {
public:
    DockDropEdgeBandClassifier() = delete;

    [[nodiscard]] static DockDropZone Classify(const DockRect& rect, int x, int y, int band) noexcept;
};

} // namespace kb::editor
