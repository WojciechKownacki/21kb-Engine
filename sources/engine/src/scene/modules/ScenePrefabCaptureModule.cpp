#include "engine/scene/ScenePrefabs.hpp"

#include "scene/prefab/ScenePrefabCaptureFacade.hpp"

#include <utility>

namespace kb::scene {

ScenePrefab ScenePrefabs::Capture(SceneObject root) const {
    return Capture(root, ScenePrefabCaptureSettings{});
}

ScenePrefab ScenePrefabs::Capture(SceneObject root, const ScenePrefabCaptureSettings& settings) const {
    return ScenePrefabCaptureFacade::Capture(scene_, root, settings);
}

ScenePrefabHandle ScenePrefabs::CaptureRegistered(SceneObject root, std::string name) {
    return CaptureRegistered(root, ScenePrefabCaptureSettings{}, std::move(name));
}

ScenePrefabHandle ScenePrefabs::CaptureRegistered(SceneObject root, const ScenePrefabCaptureSettings& settings, std::string name) {
    return ScenePrefabCaptureFacade::CaptureRegistered(scene_, root, settings, std::move(name));
}

} // namespace kb::scene
