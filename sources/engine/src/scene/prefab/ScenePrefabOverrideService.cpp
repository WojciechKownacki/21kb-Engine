#include "scene/prefab/ScenePrefabOverrideService.hpp"

#include "scene/prefab/ScenePrefabOverrideMutationService.hpp"
#include "scene/prefab/ScenePrefabOverrideQueryService.hpp"
#include "scene/prefab/io/ScenePrefabAssetService.hpp"

namespace kb::scene {

bool ScenePrefabOverrideService::IsInstance(Scene& scene, ScenePrefabInstanceHandle handle) noexcept {
    return ScenePrefabOverrideQueryService::IsInstance(scene, handle);
}

ScenePrefabOverrideReport ScenePrefabOverrideService::Overrides(Scene& scene, ScenePrefabInstanceHandle handle) {
    return ScenePrefabOverrideQueryService::Overrides(scene, handle);
}

bool ScenePrefabOverrideService::Revert(Scene& scene, ScenePrefabInstanceHandle handle) {
    return ScenePrefabOverrideMutationService::Revert(scene, handle);
}

bool ScenePrefabOverrideService::Apply(Scene& scene, ScenePrefabInstanceHandle handle) {
    return ScenePrefabOverrideMutationService::Apply(scene, handle);
}

bool ScenePrefabOverrideService::Apply(Scene& scene, std::span<const ScenePrefabInstanceHandle> handles) {
    return ScenePrefabOverrideMutationService::Apply(scene, handles);
}

bool ScenePrefabOverrideService::ApplyAndSave(Scene& scene, ScenePrefabInstanceHandle handle, const std::filesystem::path& assetPath) {
    return Apply(scene, handle) && ScenePrefabAssetService::SaveInstancePrefab(scene, handle, assetPath);
}

bool ScenePrefabOverrideService::RevertProperty(Scene& scene, ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string_view propertyPath) {
    return ScenePrefabOverrideMutationService::RevertProperty(scene, handle, nodeIndex, propertyPath);
}

bool ScenePrefabOverrideService::ApplyProperty(Scene& scene, ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string_view propertyPath) {
    return ScenePrefabOverrideMutationService::ApplyProperty(scene, handle, nodeIndex, propertyPath);
}

bool ScenePrefabOverrideService::ApplyPropertyAndSave(Scene& scene, ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string_view propertyPath, const std::filesystem::path& assetPath) {
    return ApplyProperty(scene, handle, nodeIndex, propertyPath) && ScenePrefabAssetService::SaveInstancePrefab(scene, handle, assetPath);
}

} // namespace kb::scene
