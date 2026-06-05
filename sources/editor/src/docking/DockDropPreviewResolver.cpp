#include "docking/DockDropPreviewResolver.hpp"

#include "docking/DockDropPreviewFactory.hpp"
#include "docking/DockDropZoneClassifier.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] bool IsLeafDockZone(DockDropZone zone) noexcept {
    return zone == DockDropZone::Left || zone == DockDropZone::Right || zone == DockDropZone::Top || zone == DockDropZone::Bottom;
}

[[nodiscard]] bool IsPrimaryTopChromeDrop(const DockLayout& layout, int x, int y) noexcept {
    return !layout.workspace.Empty() &&
           x >= layout.workspace.x &&
           x < layout.workspace.x + layout.workspace.width &&
           y >= 0 &&
           y < layout.workspace.y;
}

} // namespace

std::optional<DockDropPreview> DockDropPreviewResolver::Resolve(const DockLayout& layout, int x, int y) const {
    const DockDropPreviewFactory previewFactory;
    if (IsPrimaryTopChromeDrop(layout, x, y)) {
        return previewFactory.ForRootEdge(layout.workspace, DockDropZone::Top);
    }

    if (!layout.workspace.Contains(x, y)) {
        return std::nullopt;
    }

    const DockDropZoneClassifier classifier;

    for (auto it = layout.leaves.rbegin(); it != layout.leaves.rend(); ++it) {
        if (!it->frame.Contains(x, y)) {
            continue;
        }
        if (it->tabStrip.Contains(x, y)) {
            return previewFactory.ForStripMarker(layout, *it, x);
        }
        if (it->content.Contains(x, y)) {
            const DockDropZone outerZone = classifier.DominantOuterEdge(layout.workspace, x, y);
            if (outerZone != DockDropZone::None) {
                return previewFactory.ForRootEdge(layout.workspace, outerZone);
            }

            const DockDropZone leafZone = classifier.ClassifyLeafZone(it->content, x, y);
            return IsLeafDockZone(leafZone) ? std::optional<DockDropPreview>{ previewFactory.ForLeafEdge(*it, leafZone) } : std::nullopt;
        }
        return std::nullopt;
    }

    return previewFactory.ForEmptyWorkspace(layout.workspace);
}

} // namespace kb::editor
