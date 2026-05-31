#include "scene/prefab/ScenePrefabCaptureFacade.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabCaptureService.hpp"

#include <utility>

namespace kb::scene {

ScenePrefab ScenePrefabCaptureFacade::Capture(Scene& scene, SceneObject root, const ScenePrefabCaptureSettings& settings) {
    return ScenePrefabCaptureService::Capture(scene, root, settings);
}

ScenePrefabHandle ScenePrefabCaptureFacade::CaptureRegistered(Scene& scene, SceneObject root, const ScenePrefabCaptureSettings& settings, std::string name) {
    ScenePrefab prefab = Capture(scene, root, settings);
    return SceneAccess::State(scene).prefabs.Register(std::move(name), std::move(prefab));
}

} // namespace kb::scene
