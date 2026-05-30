#include "docking/DockSplitterResizer.hpp"

#include "docking/DockGeometry.hpp"
#include "docking/DockNodeQuery.hpp"

#include <algorithm>

namespace kb::editor {

void DockSplitterResizer::Resize(DockNode* root, std::uint32_t nodeId, int mouseX, int mouseY, const DockLayout& layout) noexcept {
    DockNode* node = DockNodeQuery::FindNode(root, nodeId);
    if (node == nullptr || node->kind != DockNode::Kind::Split) {
        return;
    }

    const auto it = std::find_if(layout.splitters.begin(), layout.splitters.end(), [nodeId](const DockSplitterLayout& splitter) {
        return splitter.nodeId == nodeId;
    });
    if (it == layout.splitters.end()) {
        return;
    }

    if (node->axis == DockSplitAxis::Horizontal) {
        node->ratio = DockGeometry::ClampRatio(static_cast<float>(mouseX - it->container.x) / static_cast<float>(std::max(1, it->container.width)));
    } else {
        node->ratio = DockGeometry::ClampRatio(static_cast<float>(mouseY - it->container.y) / static_cast<float>(std::max(1, it->container.height)));
    }
}

} // namespace kb::editor
