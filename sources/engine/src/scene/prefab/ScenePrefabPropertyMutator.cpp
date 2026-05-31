#include "scene/prefab/ScenePrefabPropertyMutator.hpp"

#include "scene/prefab/ScenePrefabPropertyApplier.hpp"
#include "scene/prefab/ScenePrefabPropertyReverter.hpp"

namespace kb::scene {

bool ScenePrefabPropertyMutator::Revert(Scene& scene, const ScenePrefabInstanceRecord& instance, SceneObject object, const ScenePrefabNodeDesc& node, std::string_view propertyPath) {
    return ScenePrefabPropertyReverter::Revert(scene, instance, object, node, propertyPath);
}

bool ScenePrefabPropertyMutator::Apply(Scene& scene, ScenePrefabNodeDesc& node, SceneObject object, std::string_view propertyPath) {
    return ScenePrefabPropertyApplier::Apply(scene, node, object, propertyPath);
}

} // namespace kb::scene
