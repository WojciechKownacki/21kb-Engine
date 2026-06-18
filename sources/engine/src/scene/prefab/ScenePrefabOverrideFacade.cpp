#include "scene/prefab/ScenePrefabOverrideFacade.hpp"

#include "scene/prefab/ScenePrefabOverrideService.hpp"

namespace kb::scene {

bool ScenePrefabOverrideFacade::IsInstance(Scene& scene, ScenePrefabInstanceHandle handle) noexcept {
    return ScenePrefabOverrideService::IsInstance(scene, handle);
}

ScenePrefabOverrideReport ScenePrefabOverrideFacade::Overrides(Scene& scene, ScenePrefabInstanceHandle handle) {
    return ScenePrefabOverrideService::Overrides(scene, handle);
}

bool ScenePrefabOverrideFacade::Revert(Scene& scene, ScenePrefabInstanceHandle handle) {
    return ScenePrefabOverrideService::Revert(scene, handle);
}

bool ScenePrefabOverrideFacade::Apply(Scene& scene, ScenePrefabInstanceHandle handle) {
    return ScenePrefabOverrideService::Apply(scene, handle);
}

bool ScenePrefabOverrideFacade::Apply(Scene& scene, std::span<const ScenePrefabInstanceHandle> handles) {
    return ScenePrefabOverrideService::Apply(scene, handles);
}

bool ScenePrefabOverrideFacade::ApplyAndSave(Scene& scene, ScenePrefabInstanceHandle handle, const std::filesystem::path& assetPath) {
    return ScenePrefabOverrideService::ApplyAndSave(scene, handle, assetPath);
}

bool ScenePrefabOverrideFacade::RevertProperty(Scene& scene, ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string propertyPath) {
    return ScenePrefabOverrideService::RevertProperty(scene, handle, nodeIndex, propertyPath);
}

bool ScenePrefabOverrideFacade::ApplyProperty(Scene& scene, ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string propertyPath) {
    return ScenePrefabOverrideService::ApplyProperty(scene, handle, nodeIndex, propertyPath);
}

bool ScenePrefabOverrideFacade::ApplyPropertyAndSave(Scene& scene, ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string propertyPath, const std::filesystem::path& assetPath) {
    return ScenePrefabOverrideService::ApplyPropertyAndSave(scene, handle, nodeIndex, propertyPath, assetPath);
}

} // namespace kb::scene
