#include "scene/prefab/ScenePrefabVariantMaterializer.hpp"

#include "scene/prefab/ScenePrefabPropertyOverrideApplier.hpp"
#include "scene/prefab/ScenePrefabValidator.hpp"

#include <utility>

namespace kb::scene {

bool ScenePrefabVariantMaterializer::Materialize(const ScenePrefab& basePrefab, std::span<const ScenePrefabPropertyOverride> overrides, ScenePrefab& output) {
    ScenePrefab variant = basePrefab;
    for (const ScenePrefabPropertyOverride& property : overrides) {
        ScenePrefabNodeDesc* node = property.nodeId != ScenePrefabNodeDesc::InvalidStableId
            ? variant.TryGetMutableNodeByStableId(property.nodeId)
            : variant.TryGetMutableNode(property.nodeIndex);
        if (node == nullptr || !ScenePrefabPropertyOverrideApplier::Apply(*node, property)) {
            return false;
        }
    }
    if (!ScenePrefabValidator::IsValid(variant) || variant.Empty()) {
        return false;
    }

    output = std::move(variant);
    return true;
}

} // namespace kb::scene
