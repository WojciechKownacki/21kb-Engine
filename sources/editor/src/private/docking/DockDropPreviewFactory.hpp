#pragma once

#include "kb/editor/docking/DockTypes.hpp"

namespace kb::editor {

class DockDropPreviewFactory {
public:
    [[nodiscard]] DockDropPreview ForStripMarker(
        const DockLayout& layout,
        const DockLeafLayout& leaf,
        int x) const noexcept;
    [[nodiscard]] DockDropPreview ForRootEdge(const DockRect& workspace, DockDropZone zone) const noexcept;
    [[nodiscard]] DockDropPreview ForLeafEdge(const DockLeafLayout& leaf, DockDropZone zone) const noexcept;
    [[nodiscard]] DockDropPreview ForEmptyWorkspace(const DockRect& workspace) const noexcept;

private:
    [[nodiscard]] static std::uint32_t StripInsertionIndex(const DockLayout& layout, std::uint32_t leafId, int x) noexcept;
    [[nodiscard]] static int StripMarkerX(
        const DockLayout& layout,
        const DockLeafLayout& leaf,
        std::uint32_t insertionIndex) noexcept;
};

} // namespace kb::editor
