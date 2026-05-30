#include "engine/scene/ScenePrefabs.hpp"

#include "scene/prefab/ScenePrefabCaptureService.hpp"
#include "scene/prefab/ScenePrefabInstantiationService.hpp"

namespace kb::scene {

ScenePrefabs::ScenePrefabs(Scene& scene) noexcept
    : scene_(scene) {}

ScenePrefab ScenePrefabs::Capture(SceneObject root) const {
    return Capture(root, ScenePrefabCaptureSettings{});
}

ScenePrefab ScenePrefabs::Capture(SceneObject root, const ScenePrefabCaptureSettings& settings) const {
    return ScenePrefabCaptureService::Capture(scene_, root, settings);
}

ScenePrefabInstance ScenePrefabs::Instantiate(const ScenePrefab& prefab) {
    return Instantiate(prefab, ScenePrefabInstantiationSettings{});
}

ScenePrefabInstance ScenePrefabs::Instantiate(const ScenePrefab& prefab, const ScenePrefabInstantiationSettings& settings) {
    return ScenePrefabInstantiationService::Instantiate(scene_, prefab, settings);
}

} // namespace kb::scene
