#include "engine/scene/ScenePrefabs.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabRecord.hpp"
#include "scene/prefab/ScenePrefabRegistryFacade.hpp"

#include <utility>

namespace kb::scene {

ScenePrefabHandle ScenePrefabs::Register(std::string name, ScenePrefab prefab) {
    return ScenePrefabRegistryFacade::Register(scene_, std::move(name), std::move(prefab));
}

ScenePrefabHandle ScenePrefabs::RegisterVariant(std::string name, ScenePrefabHandle basePrefab, std::vector<ScenePrefabPropertyOverride> overrides) {
    return ScenePrefabRegistryFacade::RegisterVariant(scene_, std::move(name), basePrefab, std::move(overrides));
}

bool ScenePrefabs::Contains(ScenePrefabHandle handle) const noexcept {
    return ScenePrefabRegistryFacade::Contains(scene_, handle);
}

std::string ScenePrefabs::Guid(ScenePrefabHandle handle) const {
    return ScenePrefabRegistryFacade::Guid(scene_, handle);
}

std::filesystem::path ScenePrefabs::SourcePath(ScenePrefabHandle handle) const {
    const ScenePrefabRecord* record = SceneAccess::State(scene_).prefabs.FindRecord(handle);
    return record == nullptr ? std::filesystem::path{} : std::filesystem::path{ record->sourcePath };
}

std::size_t ScenePrefabs::RegisteredCount() const noexcept {
    return ScenePrefabRegistryFacade::Count(scene_);
}

ScenePrefab ScenePrefabs::Get(ScenePrefabHandle handle) const {
    return ScenePrefabRegistryFacade::Get(scene_, handle);
}

} // namespace kb::scene
