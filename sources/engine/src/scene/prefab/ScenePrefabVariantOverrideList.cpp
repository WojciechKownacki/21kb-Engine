#include "scene/prefab/ScenePrefabVariantOverrideList.hpp"

#include <algorithm>
#include <utility>

namespace kb::scene {

bool ScenePrefabVariantOverrideList::Upsert(std::vector<ScenePrefabPropertyOverride> source, ScenePrefabPropertyOverride property, std::vector<ScenePrefabPropertyOverride>& output) {
    if (property.propertyPath.empty()) {
        return false;
    }

    const auto sameProperty = [&property](const ScenePrefabPropertyOverride& existing) {
        if (existing.nodeId != 0 && property.nodeId != 0) {
            return existing.nodeId == property.nodeId && existing.propertyPath == property.propertyPath;
        }
        return existing.nodeIndex == property.nodeIndex && existing.propertyPath == property.propertyPath;
    };
    const auto iterator = std::find_if(source.begin(), source.end(), sameProperty);
    if (iterator == source.end()) {
        source.push_back(std::move(property));
    } else {
        *iterator = std::move(property);
    }

    output = std::move(source);
    return true;
}

std::vector<ScenePrefabPropertyOverride> ScenePrefabVariantOverrideList::Normalize(std::vector<ScenePrefabPropertyOverride> overrides) {
    std::vector<ScenePrefabPropertyOverride> canonical;
    canonical.reserve(overrides.size());
    for (ScenePrefabPropertyOverride& property : overrides) {
        // Empty-propertyPath entries are not canonical overrides; skip them
        // up front so the Upsert below always succeeds (it would otherwise
        // refuse and leave `folded` unset, dropping the accumulated list).
        if (property.propertyPath.empty()) {
            continue;
        }
        // Fold each entry in with the same last-write-wins, position-
        // preserving rule the instance-apply path uses.
        std::vector<ScenePrefabPropertyOverride> folded;
        static_cast<void>(ScenePrefabVariantOverrideList::Upsert(std::move(canonical), std::move(property), folded));
        canonical = std::move(folded);
    }
    return canonical;
}

} // namespace kb::scene
