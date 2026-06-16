#include "scene/prefab/ScenePrefabNestedOverrideApplicator.hpp"

#include "engine/scene/ScenePrefabOverrides.hpp"
#include "scene/prefab/ScenePrefabPropertyOverrideApplier.hpp"

namespace kb::scene {

void ScenePrefabNestedOverrideApplicator::Apply(ScenePrefabNodeDesc& node, std::uint32_t nestedNodeIndex, std::uint64_t nestedNodeId, const ScenePrefabNodeDesc& overlayRoot) {
    for (const ScenePrefabPropertyOverride& property : overlayRoot.nestedPrefabOverrides) {
        const bool matchesStableId = property.nodeId != ScenePrefabNodeDesc::InvalidStableId && property.nodeId == nestedNodeId;
        const bool matchesFallbackIndex = property.nodeId == ScenePrefabNodeDesc::InvalidStableId && property.nodeIndex == nestedNodeIndex;
        if (matchesStableId || matchesFallbackIndex) {
            static_cast<void>(ScenePrefabPropertyOverrideApplier::Apply(node, property));
        }
    }
}

} // namespace kb::scene
