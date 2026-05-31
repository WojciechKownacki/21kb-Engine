#include "scene/prefab/ScenePrefabComponentOverrideReporter.hpp"

#include "scene/prefab/ScenePrefabCameraOverrideReporter.hpp"
#include "scene/prefab/ScenePrefabLightOverrideReporter.hpp"
#include "scene/prefab/ScenePrefabMeshRendererOverrideReporter.hpp"

namespace kb::scene {

void ScenePrefabComponentOverrideReporter::Append(SceneComponents components, SceneEntity entity, const ScenePrefabNodeComponents& expected, ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject object) {
    ScenePrefabCameraOverrideReporter::Append(components, entity, expected.camera, report, nodeIndex, object);
    ScenePrefabMeshRendererOverrideReporter::Append(components, entity, expected.meshRenderer, report, nodeIndex, object);
    ScenePrefabLightOverrideReporter::Append(components, entity, expected.light, report, nodeIndex, object);
}

} // namespace kb::scene
