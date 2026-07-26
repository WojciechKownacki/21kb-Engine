#include "scene/prefab/ScenePrefabValidator.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace kb::scene {

bool ScenePrefabValidator::IsValid(const ScenePrefab& prefab) noexcept {
    const std::span<const ScenePrefabNodeDesc> nodes = prefab.Nodes();
    std::vector<std::uint64_t> stableIds;
    stableIds.reserve(nodes.size());
    for (std::uint32_t nodeIndex = 0; nodeIndex < static_cast<std::uint32_t>(nodes.size()); ++nodeIndex) {
        const std::uint32_t parentNode = nodes[nodeIndex].parentNode;
        if (parentNode != ScenePrefabNodeDesc::NoParent && parentNode >= nodeIndex) {
            return false;
        }
        if (nodes[nodeIndex].stableId == ScenePrefabNodeDesc::InvalidStableId ||
            nodes[nodeIndex].stableId == ScenePrefabJointComponent::UnresolvedConnectedNodeStableId) {
            return false;
        }
        stableIds.push_back(nodes[nodeIndex].stableId);
    }
    std::sort(stableIds.begin(), stableIds.end());
    if (std::adjacent_find(stableIds.begin(), stableIds.end()) != stableIds.end()) {
        return false;
    }
    for (const ScenePrefabNodeDesc& node : nodes) {
        if (!node.components.joint.has_value()) {
            continue;
        }
        const std::uint64_t targetStableId = node.components.joint->connectedNodeStableId;
        if (targetStableId == ScenePrefabJointComponent::InvalidConnectedNodeStableId) {
            continue;
        }
        if (targetStableId == node.stableId || !std::binary_search(stableIds.begin(), stableIds.end(), targetStableId)) {
            return false;
        }
    }
    return true;
}

} // namespace kb::scene
