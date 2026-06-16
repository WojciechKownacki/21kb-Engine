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

} // namespace kb::scene
