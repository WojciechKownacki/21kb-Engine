#include "scene/prefab/ScenePrefabNameResolver.hpp"

namespace kb::scene {

std::string ScenePrefabNameResolver::Resolve(const ScenePrefabNodeDesc& node, const ScenePrefabInstantiationSettings& settings) {
    if (settings.namePrefix.empty()) {
        return node.name;
    }
    return settings.namePrefix + node.name;
}

} // namespace kb::scene
