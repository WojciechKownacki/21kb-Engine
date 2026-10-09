#include "engine/scene/ScenePrefabs.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabRecord.hpp"
#include "scene/prefab/ScenePrefabRegistryFacade.hpp"
#include "scene/prefab/io/ScenePrefabAssetService.hpp"

#include <algorithm>
#include <vector>

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

ScenePrefabHandle ScenePrefabs::FindLoaded(const std::filesystem::path& path) const {
    return SceneAccess::State(scene_).prefabs.FindBySourcePath(ScenePrefabAssetService::SourcePathOf(path));
}

bool ScenePrefabs::UsesPrefab(ScenePrefabHandle prefab, ScenePrefabHandle used) const {
    const ScenePrefabRegistry& registry = SceneAccess::State(scene_).prefabs;
    std::vector<ScenePrefabHandle> pending{ prefab };
    std::vector<ScenePrefabHandle> visited;
    while (!pending.empty()) {
        const ScenePrefabHandle handle = pending.back();
        pending.pop_back();
        if (handle == used) {
            return true;
        }
        const ScenePrefabRecord* record = registry.FindRecord(handle);
        if (record == nullptr || std::ranges::find(visited, handle) != visited.end()) {
            continue;
        }
        visited.push_back(handle);
        if (record->kind == ScenePrefabRecordKind::Variant) {
            pending.push_back(record->basePrefab);
        }
        for (const ScenePrefabNodeDesc& node : record->prefab.Nodes()) {
            if (!node.nestedPrefabGuid.empty()) {
                pending.push_back(registry.FindByGuid(node.nestedPrefabGuid));
            }
        }
    }
    return false;
}

std::size_t ScenePrefabs::RegisteredCount() const noexcept {
    return ScenePrefabRegistryFacade::Count(scene_);
}

ScenePrefab ScenePrefabs::Get(ScenePrefabHandle handle) const {
    return ScenePrefabRegistryFacade::Get(scene_, handle);
}

} // namespace kb::scene
