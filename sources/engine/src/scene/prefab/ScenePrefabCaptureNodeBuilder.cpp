#include "scene/prefab/ScenePrefabCaptureNodeBuilder.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabComponentSnapshot.hpp"
#include "scene/prefab/ScenePrefabHasher.hpp"
#include "scene/prefab/ScenePrefabOverrideDetector.hpp"

namespace kb::scene {
namespace {

void ApplyNestedPrefabMetadata(Scene& scene, SceneObject object, ScenePrefabNodeDesc& node) {
    SceneState& state = SceneAccess::State(scene);
    const ScenePrefabInstanceHandle instanceHandle = state.prefabInstances.FindRootInstance(object);
    const ScenePrefabInstanceRecord* instance = state.prefabInstances.Find(instanceHandle);
    if (instance == nullptr) {
        return;
    }

    const ScenePrefabRecord* prefabRecord = state.prefabs.FindRecord(instance->prefab);
    if (prefabRecord == nullptr || prefabRecord->guid.empty()) {
        return;
    }

    node.nestedPrefabGuid = prefabRecord->guid;
    const ScenePrefab& baseline = instance->BaselineOr(prefabRecord->prefab);
    node.nestedPrefabOverrides = ScenePrefabOverrideDetector::Detect(scene, baseline, *instance).properties;
    node.nestedPrefabContentHash = ScenePrefabHasher::Hash(baseline);
}

} // namespace

ScenePrefabNodeDesc ScenePrefabCaptureNodeBuilder::Build(Scene& scene, SceneObject object, std::uint32_t parentNode) {
    ScenePrefabNodeDesc node{
        .name = object.Name(),
        .nestedPrefabGuid = {},
        .nestedPrefabOverrides = {},
        .parentNode = parentNode,
        .transform = object.Transform(),
        .visibility = scene.Components().Visibility().Get(object.Entity()),
        .components = ScenePrefabComponentSnapshot::Capture(scene, object),
    };
    node.SetLocalTranslation(scene.Transforms().LocalTranslation(object.Entity()));
    ApplyNestedPrefabMetadata(scene, object, node);
    return node;
}

} // namespace kb::scene
