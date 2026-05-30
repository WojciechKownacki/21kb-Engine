#include "scene/prefab/ScenePrefabCaptureTraversal.hpp"

#include "engine/scene/ScenePrefab.hpp"
#include "scene/prefab/ScenePrefabCaptureNodeBuilder.hpp"
#include "scene/prefab/ScenePrefabChildrenReader.hpp"

namespace kb::scene {

void ScenePrefabCaptureTraversal::Append(Scene& scene, SceneObject object, const ScenePrefabCaptureSettings& settings, ScenePrefab& prefab, std::uint32_t parentNode) {
    const std::uint32_t nodeIndex = prefab.AddNode(ScenePrefabCaptureNodeBuilder::Build(scene, object, parentNode));

    for (const SceneObject child : ScenePrefabChildrenReader::Read(object, settings)) {
        Append(scene, child, settings, prefab, nodeIndex);
    }
}

} // namespace kb::scene
