#include "scene/prefab/ScenePrefabNestedOverrideApplicator.hpp"

#include "engine/scene/ScenePrefabOverrides.hpp"
#include "scene/prefab/ScenePrefabPropertyOverrideApplier.hpp"

namespace kb::scene {

void ScenePrefabNestedOverrideApplicator::Apply(ScenePrefabNodeDesc& node, std::uint32_t nestedNodeIndex, const ScenePrefabNodeDesc& overlayRoot) {
    for (const ScenePrefabPropertyOverride& property : overlayRoot.nestedPrefabOverrides) {
        if (property.nodeIndex == nestedNodeIndex) {
            static_cast<void>(ScenePrefabPropertyOverrideApplier::Apply(node, property));
        }
    }
}

} // namespace kb::scene
