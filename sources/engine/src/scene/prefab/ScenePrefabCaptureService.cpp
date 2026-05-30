#include "scene/prefab/ScenePrefabCaptureService.hpp"

#include "scene/prefab/ScenePrefabCaptureValidator.hpp"
#include "scene/prefab/ScenePrefabCaptureTraversal.hpp"
#include "scene/prefab/ScenePrefabHierarchyCounter.hpp"

namespace kb::scene {

ScenePrefab ScenePrefabCaptureService::Capture(Scene& scene, SceneObject root, const ScenePrefabCaptureSettings& settings) {
    ScenePrefab prefab;
    if (!ScenePrefabCaptureValidator::CanCapture(scene, root)) {
        return prefab;
    }

    prefab.Reserve(ScenePrefabHierarchyCounter::Count(root, settings));
    ScenePrefabCaptureTraversal::Append(scene, root, settings, prefab, ScenePrefabNodeDesc::NoParent);
    return prefab;
}

} // namespace kb::scene
