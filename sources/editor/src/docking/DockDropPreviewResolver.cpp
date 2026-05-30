#include "docking/DockDropPreviewResolver.hpp"

#include "docking/DockDropPreviewFactory.hpp"
#include "docking/DockDropZoneClassifier.hpp"

namespace kb::editor {

std::optional<DockDropPreview> DockDropPreviewResolver::Resolve(const DockLayout& layout, int x, int y) const {
    if (!layout.workspace.Contains(x, y)) {
        return std::nullopt;
    }

    const DockDropZoneClassifier classifier;
    const DockDropPreviewFactory previewFactory;

    for (auto it = layout.leaves.rbegin(); it != layout.leaves.rend(); ++it) {
        if (!it->frame.Contains(x, y)) {
            continue;
        }
        if (it->tabStrip.Contains(x, y)) {
            return previewFactory.ForStripMarker(*it);
        }
        if (it->content.Contains(x, y)) {
            const DockDropZone outerZone = classifier.DominantOuterEdge(layout.workspace, x, y);
            if (outerZone != DockDropZone::None) {
                return previewFactory.ForRootEdge(layout.workspace, outerZone);
            }

            const DockDropZone leafZone = classifier.ClassifyLeafZone(it->content, x, y);
            return leafZone == DockDropZone::Center ? std::nullopt : std::optional<DockDropPreview>{ previewFactory.ForLeafEdge(*it, leafZone) };
        }
        return std::nullopt;
    }

    return previewFactory.ForEmptyWorkspace(layout.workspace);
}

} // namespace kb::editor
