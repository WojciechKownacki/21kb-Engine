#include "scene/prefab/ScenePrefabParentResolver.hpp"

namespace kb::scene {

SceneObject ScenePrefabParentResolver::Resolve(const ScenePrefabNodeDesc& node, const ScenePrefabInstantiationSettings& settings, std::span<const SceneObject> createdObjects) noexcept {
    if (node.parentNode == ScenePrefabNodeDesc::NoParent) {
        return settings.parent;
    }
    return node.parentNode < createdObjects.size() ? createdObjects[node.parentNode] : SceneObject{};
}

} // namespace kb::scene
