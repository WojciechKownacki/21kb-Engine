#include "docking/DockNodeLayoutBuilder.hpp"

#include "docking/DockLeafLayoutBuilder.hpp"
#include "docking/DockSplitLayoutBuilder.hpp"

namespace kb::editor {

void DockNodeLayoutBuilder::Build(const DockNode& node, const DockRect& rect, DockLayout& layout, const DockLayoutBuildSettings& settings) const {
    if (node.kind == DockNode::Kind::Leaf) {
        DockLeafLayoutBuilder{}.Build(node, rect, layout, settings);
        return;
    }

    const DockSplitLayoutParts parts = DockSplitLayoutBuilder{}.Build(node, rect, settings);
    if (node.first != nullptr) {
        Build(*node.first, parts.first, layout, settings);
    }

    layout.splitters.push_back(DockSplitterLayout{
        .nodeId = node.id,
        .axis = node.axis,
        .rect = parts.splitter,
        .container = rect,
    });

    if (node.second != nullptr) {
        Build(*node.second, parts.second, layout, settings);
    }
}

} // namespace kb::editor
