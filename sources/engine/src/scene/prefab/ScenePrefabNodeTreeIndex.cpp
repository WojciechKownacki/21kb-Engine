#include "scene/prefab/ScenePrefabNodeTreeIndex.hpp"

#include <span>

namespace kb::scene {

ScenePrefabNodeTreeIndex::ScenePrefabNodeTreeIndex(const ScenePrefab& prefab)
    : children_(prefab.NodeCount()) {
    const std::span<const ScenePrefabNodeDesc> nodes = prefab.Nodes();
    for (std::uint32_t index = 0; index < static_cast<std::uint32_t>(nodes.size()); ++index) {
        const std::uint32_t parent = nodes[index].parentNode;
        if (parent != ScenePrefabNodeDesc::NoParent && parent < children_.size()) {
            children_[parent].push_back(index);
        }
    }
}

const std::vector<std::uint32_t>& ScenePrefabNodeTreeIndex::Children(std::uint32_t nodeIndex) const {
    return children_[nodeIndex];
}

std::vector<std::uint32_t> ScenePrefabNodeTreeIndex::CollectPreorder(std::uint32_t rootNodeIndex) const {
    std::vector<std::uint32_t> nodes;
    CollectPreorder(rootNodeIndex, nodes);
    return nodes;
}

void ScenePrefabNodeTreeIndex::CollectPreorder(std::uint32_t nodeIndex, std::vector<std::uint32_t>& output) const {
    output.push_back(nodeIndex);
    for (const std::uint32_t child : children_[nodeIndex]) {
        CollectPreorder(child, output);
    }
}

} // namespace kb::scene
