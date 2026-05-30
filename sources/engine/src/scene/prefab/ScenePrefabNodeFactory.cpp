#include "scene/prefab/ScenePrefabNodeFactory.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "scene/prefab/ScenePrefabComponentApplier.hpp"
#include "scene/prefab/ScenePrefabNameResolver.hpp"
#include "scene/prefab/ScenePrefabParentResolver.hpp"

namespace kb::scene {

SceneObject ScenePrefabNodeFactory::Create(Scene& scene, const ScenePrefabNodeDesc& node, const ScenePrefabInstantiationSettings& settings, std::span<const SceneObject> createdObjects) {
    SceneObject object = scene.Entities().CreateObject(SceneObjectDesc{
        .name = ScenePrefabNameResolver::Resolve(node, settings),
        .parent = ScenePrefabParentResolver::Resolve(node, settings, createdObjects),
        .transform = node.transform,
        .visibility = node.visibility,
    });
    ScenePrefabComponentApplier::Apply(scene, object, node.components);
    return object;
}

} // namespace kb::scene
