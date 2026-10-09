#include "scene/prefab/ScenePrefabNodeFactory.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "scene/prefab/ScenePrefabComponentApplier.hpp"
#include "scene/prefab/ScenePrefabNameResolver.hpp"
#include "scene/prefab/ScenePrefabParentResolver.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/transform/SceneTransformPrecision.hpp"

namespace kb::scene {

SceneObject ScenePrefabNodeFactory::Create(Scene& scene, const ScenePrefabNodeDesc& node, const ScenePrefabInstantiationSettings& settings, std::span<const SceneObject> createdObjects) {
    SceneObject object = scene.Entities().CreateObject(SceneObjectDesc{
        .name = ScenePrefabNameResolver::Resolve(node, settings),
        .parent = ScenePrefabParentResolver::Resolve(node, settings, createdObjects),
        .transform = node.transform,
        .visibility = node.visibility,
    });
    SceneTransformPrecision::StoreLocalResidual(SceneAccess::State(scene), object.Entity(),
        FittingResidual(node.transform.localPosition, node.localPositionResidual));
    ScenePrefabComponentApplier::Apply(scene, object, node.components);
    return object;
}

} // namespace kb::scene
