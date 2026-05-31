#include "scene/prefab/io/ScenePrefabAssetService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/io/ScenePrefabAssetReader.hpp"
#include "scene/prefab/io/ScenePrefabAssetWriter.hpp"

#include <utility>

namespace kb::scene {

bool ScenePrefabAssetService::Save(Scene& scene, ScenePrefabHandle handle, const std::filesystem::path& path) {
    const ScenePrefabRecord* record = SceneAccess::State(scene).prefabs.FindRecord(handle);
    if (record == nullptr) {
        return false;
    }

    return ScenePrefabAssetWriter::Write(
        path,
        ScenePrefabAssetWriteDesc{
            .kind = record->kind == ScenePrefabRecordKind::Template ? ScenePrefabAssetKind::Template : ScenePrefabAssetKind::Variant,
            .guid = record->guid,
            .name = record->name,
            .baseGuid = record->basePrefabGuid,
            .prefab = &record->prefab,
            .overrides = &record->variantOverrides,
        });
}

ScenePrefabHandle ScenePrefabAssetService::Load(Scene& scene, const std::filesystem::path& path) {
    ScenePrefabAssetReadResult asset;
    if (!ScenePrefabAssetReader::Read(path, asset)) {
        return {};
    }
    if (asset.kind == ScenePrefabAssetKind::Variant) {
        return SceneAccess::State(scene).prefabs.RegisterLoadedVariant(std::move(asset.guid), std::move(asset.name), std::move(asset.baseGuid), std::move(asset.overrides));
    }
    if (asset.guid.empty()) {
        return SceneAccess::State(scene).prefabs.Register(std::move(asset.name), std::move(asset.prefab));
    }
    return SceneAccess::State(scene).prefabs.RegisterLoaded(std::move(asset.guid), std::move(asset.name), std::move(asset.prefab));
}

bool ScenePrefabAssetService::SaveInstancePrefab(Scene& scene, ScenePrefabInstanceHandle handle, const std::filesystem::path& path) {
    SceneState& state = SceneAccess::State(scene);
    const ScenePrefabInstanceRecord* instance = state.prefabInstances.Find(handle);
    return instance != nullptr && Save(scene, instance->prefab, path);
}

} // namespace kb::scene
