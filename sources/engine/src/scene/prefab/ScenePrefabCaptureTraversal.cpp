#include "scene/prefab/ScenePrefabCaptureTraversal.hpp"

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "scene/prefab/ScenePrefabCaptureNodeBuilder.hpp"
#include "scene/prefab/ScenePrefabChildrenReader.hpp"
#include <utility>

namespace kb::scene {

void ScenePrefabCaptureTraversal::Append(Scene& scene, SceneObject object, const ScenePrefabCaptureSettings& settings, ScenePrefab& prefab, std::uint32_t parentNode, std::vector<SceneEntity>& capturedEntities) {
    const auto* renderer = std::as_const(scene).Components().MeshRenderers().TryGet(object.Entity());
    if (renderer && renderer->transient) return;
    const std::uint32_t nodeIndex = prefab.AddNode(ScenePrefabCaptureNodeBuilder::Build(scene, object, parentNode));
    capturedEntities.push_back(object.Entity());

    for (const SceneObject child : ScenePrefabChildrenReader::Read(object, settings)) {
        Append(scene, child, settings, prefab, nodeIndex, capturedEntities);
    }
}

} // namespace kb::scene
