#include "scene/prefab/io/ScenePrefabAssetService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/io/ScenePrefabAssetReader.hpp"
#include "scene/prefab/io/ScenePrefabAssetWriter.hpp"

#include <utility>

namespace kb::scene {

bool ScenePrefabAssetService::Save(Scene& scene, ScenePrefabHandle handle, const std::filesystem::path& path) {
    const ScenePrefabRecord* record = SceneAccess::State(scene).prefabs.FindRecord(handle);
    return record != nullptr && ScenePrefabAssetWriter::Write(path, record->name, record->prefab);
}

ScenePrefabHandle ScenePrefabAssetService::Load(Scene& scene, const std::filesystem::path& path) {
    ScenePrefabAssetReadResult asset;
    if (!ScenePrefabAssetReader::Read(path, asset)) {
        return {};
    }
    return SceneAccess::State(scene).prefabs.Register(std::move(asset.name), std::move(asset.prefab));
}

} // namespace kb::scene
