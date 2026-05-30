#include "scene/prefab/ScenePrefabValidator.hpp"

namespace kb::scene {

bool ScenePrefabValidator::IsValid(const ScenePrefab& prefab) noexcept {
    const std::span<const ScenePrefabNodeDesc> nodes = prefab.Nodes();
    for (std::uint32_t nodeIndex = 0; nodeIndex < static_cast<std::uint32_t>(nodes.size()); ++nodeIndex) {
        const std::uint32_t parentNode = nodes[nodeIndex].parentNode;
        if (parentNode != ScenePrefabNodeDesc::NoParent && parentNode >= nodeIndex) {
            return false;
        }
    }
    return true;
}

} // namespace kb::scene
