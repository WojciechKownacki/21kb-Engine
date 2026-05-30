#include "scene/prefab/ScenePrefabInstantiationService.hpp"

#include "scene/prefab/ScenePrefabNodeFactory.hpp"
#include "scene/prefab/ScenePrefabValidator.hpp"

namespace kb::scene {

ScenePrefabInstance ScenePrefabInstantiationService::Instantiate(Scene& scene, const ScenePrefab& prefab, const ScenePrefabInstantiationSettings& settings) {
    if (!ScenePrefabValidator::IsValid(prefab)) {
        return ScenePrefabInstance{};
    }

    const std::span<const ScenePrefabNodeDesc> nodes = prefab.Nodes();
    std::vector<SceneObject> objects;
    objects.reserve(nodes.size());

    for (const ScenePrefabNodeDesc& node : nodes) {
        objects.push_back(ScenePrefabNodeFactory::Create(scene, node, settings, objects));
    }

    return ScenePrefabInstance{ std::move(objects) };
}

} // namespace kb::scene
