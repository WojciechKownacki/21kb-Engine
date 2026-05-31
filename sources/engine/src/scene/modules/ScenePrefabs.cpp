#include "engine/scene/ScenePrefabs.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabCaptureService.hpp"
#include "scene/prefab/io/ScenePrefabAssetService.hpp"
#include "scene/prefab/ScenePrefabInstantiationService.hpp"
#include "scene/prefab/ScenePrefabOverrideService.hpp"
#include "scene/prefab/ScenePrefabRegisteredInstantiationService.hpp"

#include <utility>

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

ScenePrefabHandle ScenePrefabs::Register(std::string name, ScenePrefab prefab) {
    return SceneAccess::State(scene_).prefabs.Register(std::move(name), std::move(prefab));
}

ScenePrefabHandle ScenePrefabs::CaptureRegistered(SceneObject root, std::string name) {
    return CaptureRegistered(root, ScenePrefabCaptureSettings{}, std::move(name));
}

ScenePrefabHandle ScenePrefabs::CaptureRegistered(SceneObject root, const ScenePrefabCaptureSettings& settings, std::string name) {
    ScenePrefab prefab = Capture(root, settings);
    return Register(std::move(name), std::move(prefab));
}

bool ScenePrefabs::Contains(ScenePrefabHandle handle) const noexcept {
    return SceneAccess::State(scene_).prefabs.Contains(handle);
}

std::size_t ScenePrefabs::RegisteredCount() const noexcept {
    return SceneAccess::State(scene_).prefabs.Count();
}

ScenePrefabInstance ScenePrefabs::Instantiate(ScenePrefabHandle handle) {
    return Instantiate(handle, ScenePrefabInstantiationSettings{});
}

ScenePrefabInstance ScenePrefabs::Instantiate(ScenePrefabHandle handle, const ScenePrefabInstantiationSettings& settings) {
    return ScenePrefabRegisteredInstantiationService::Instantiate(scene_, handle, settings);
}

bool ScenePrefabs::Save(ScenePrefabHandle handle, const std::filesystem::path& path) const {
    return ScenePrefabAssetService::Save(scene_, handle, path);
}

ScenePrefabHandle ScenePrefabs::Load(const std::filesystem::path& path) {
    return ScenePrefabAssetService::Load(scene_, path);
}

bool ScenePrefabs::IsInstance(ScenePrefabInstanceHandle handle) const noexcept {
    return ScenePrefabOverrideService::IsInstance(scene_, handle);
}

ScenePrefabOverrideReport ScenePrefabs::Overrides(ScenePrefabInstanceHandle handle) const {
    return ScenePrefabOverrideService::Overrides(scene_, handle);
}

bool ScenePrefabs::RevertOverrides(ScenePrefabInstanceHandle handle) {
    return ScenePrefabOverrideService::Revert(scene_, handle);
}

bool ScenePrefabs::ApplyOverrides(ScenePrefabInstanceHandle handle) {
    return ScenePrefabOverrideService::Apply(scene_, handle);
}

} // namespace kb::scene
