#include "docking/DockNodeFactory.hpp"

#include "docking/DockGeometry.hpp"

#include <utility>

namespace kb::editor {

std::unique_ptr<DockNode> DockNodeFactory::MakeLeaf(std::uint32_t id, std::vector<std::uint32_t> panels) {
    auto node = std::make_unique<DockNode>();
    node->kind = DockNode::Kind::Leaf;
    node->id = id;
    node->panels = std::move(panels);
    node->activePanelId = node->panels.empty() ? 0 : node->panels.front();
    return node;
}

std::unique_ptr<DockNode> DockNodeFactory::MakeSplit(std::uint32_t id, DockSplitAxis axis, float ratio, std::unique_ptr<DockNode> first, std::unique_ptr<DockNode> second) {
    auto node = std::make_unique<DockNode>();
    node->kind = DockNode::Kind::Split;
    node->id = id;
    node->axis = axis;
    node->ratio = DockGeometry::ClampRatio(ratio);
    node->first = std::move(first);
    node->second = std::move(second);
    return node;
}

} // namespace kb::editor
