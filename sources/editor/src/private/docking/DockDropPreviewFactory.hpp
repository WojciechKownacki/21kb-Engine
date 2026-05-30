#pragma once

#include "kb/editor/docking/DockTypes.hpp"

namespace kb::editor {

class DockDropPreviewFactory {
public:
    [[nodiscard]] DockDropPreview ForStripMarker(const DockLeafLayout& leaf) const noexcept;
    [[nodiscard]] DockDropPreview ForRootEdge(const DockRect& workspace, DockDropZone zone) const noexcept;
    [[nodiscard]] DockDropPreview ForLeafEdge(const DockLeafLayout& leaf, DockDropZone zone) const noexcept;
    [[nodiscard]] DockDropPreview ForEmptyWorkspace(const DockRect& workspace) const noexcept;
};

} // namespace kb::editor
