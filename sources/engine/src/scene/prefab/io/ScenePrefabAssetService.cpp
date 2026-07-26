#include "scene/prefab/io/ScenePrefabAssetService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabRegistry.hpp"
#include "scene/prefab/ScenePrefabValidator.hpp"
#include "scene/prefab/io/ScenePrefabAssetReader.hpp"
#include "scene/prefab/io/ScenePrefabAssetWriter.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace kb::scene {
namespace {

[[nodiscard]] bool FillMissingVariantOverrideNodeIds(ScenePrefabRegistry& registry, const std::string& baseGuid, std::vector<ScenePrefabPropertyOverride>& overrides) {
    const ScenePrefabHandle baseHandle = registry.FindByGuid(baseGuid);
    const ScenePrefab* basePrefab = registry.Find(baseHandle);
    if (basePrefab == nullptr) {
        return overrides.empty();
    }

    bool updated = false;
    for (ScenePrefabPropertyOverride& property : overrides) {
        if (property.nodeId != ScenePrefabNodeDesc::InvalidStableId) {
            continue;
        }

        const ScenePrefabNodeDesc* node = basePrefab->TryGetNode(property.nodeIndex);
        if (node == nullptr) {
            return false;
        }
        property.nodeId = node->stableId;
        updated = true;
    }
    return updated || !overrides.empty();
}

[[nodiscard]] bool FillMissingNestedOverrideNodeIds(ScenePrefabRegistry& registry, ScenePrefab& prefab) {
    bool unresolved = false;
    for (std::uint32_t nodeIndex = 0U; nodeIndex < prefab.NodeCount(); ++nodeIndex) {
        ScenePrefabNodeDesc* node = prefab.TryGetMutableNode(nodeIndex);
        if (node == nullptr || node->nestedPrefabOverrides.empty()) {
            continue;
        }

        bool hasMissingNodeId = false;
        for (const ScenePrefabPropertyOverride& property : node->nestedPrefabOverrides) {
            hasMissingNodeId = hasMissingNodeId || property.nodeId == ScenePrefabNodeDesc::InvalidStableId;
        }
        if (!hasMissingNodeId) {
            continue;
        }

        const ScenePrefabHandle nestedHandle = registry.FindByGuid(node->nestedPrefabGuid);
        const ScenePrefab* nestedPrefab = registry.Find(nestedHandle);
        if (nestedPrefab == nullptr) {
            unresolved = true;
            continue;
        }

        for (ScenePrefabPropertyOverride& property : node->nestedPrefabOverrides) {
            if (property.nodeId != ScenePrefabNodeDesc::InvalidStableId) {
                continue;
            }

            const ScenePrefabNodeDesc* nestedNode = nestedPrefab->TryGetNode(property.nodeIndex);
            if (nestedNode == nullptr) {
                unresolved = true;
                continue;
            }
            property.nodeId = nestedNode->stableId;
        }
    }
    return !unresolved;
}

[[nodiscard]] bool SaveMigratedAsset(Scene& scene, ScenePrefabHandle handle, const std::filesystem::path& path, bool required) {
    return !required || ScenePrefabAssetService::Save(scene, handle, path);
}

} // namespace

bool ScenePrefabAssetService::Save(Scene& scene, ScenePrefabHandle handle, const std::filesystem::path& path) {
    const ScenePrefabRecord* record = SceneAccess::State(scene).prefabs.FindRecord(handle);
    if (record == nullptr) {
        return false;
    }
    if (!ScenePrefabValidator::IsValid(record->prefab)) {
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
            .addedChildren = &record->variantAddedChildren,
        });
}

ScenePrefabHandle ScenePrefabAssetService::Load(Scene& scene, const std::filesystem::path& path) {
    ScenePrefabAssetReadResult asset;
    if (!ScenePrefabAssetReader::Read(path, asset)) {
        return {};
    }
    if (asset.kind == ScenePrefabAssetKind::Template && !ScenePrefabValidator::IsValid(asset.prefab)) {
        return {};
    }
    SceneState& state = SceneAccess::State(scene);
    if (asset.kind == ScenePrefabAssetKind::Variant) {
        const bool needsMigration = asset.missingOverrideNodeIds;
        if (needsMigration && !FillMissingVariantOverrideNodeIds(state.prefabs, asset.baseGuid, asset.overrides)) {
            return {};
        }
        ScenePrefabHandle handle = state.prefabs.RegisterLoadedVariant(std::move(asset.guid), std::move(asset.name), std::move(asset.baseGuid), std::move(asset.overrides), std::move(asset.addedChildren));
        return SaveMigratedAsset(scene, handle, path, handle.IsValid() && needsMigration) ? handle : ScenePrefabHandle{};
    }
    const bool nestedOverrideIdsMigrated = !asset.missingOverrideNodeIds || FillMissingNestedOverrideNodeIds(state.prefabs, asset.prefab);
    const bool needsMigration = asset.missingNodeStableIds || (asset.missingOverrideNodeIds && nestedOverrideIdsMigrated);
    ScenePrefabHandle handle;
    if (asset.guid.empty()) {
        handle = state.prefabs.Register(std::move(asset.name), std::move(asset.prefab));
    } else {
        handle = state.prefabs.RegisterLoaded(std::move(asset.guid), std::move(asset.name), std::move(asset.prefab));
    }
    return SaveMigratedAsset(scene, handle, path, handle.IsValid() && needsMigration) ? handle : ScenePrefabHandle{};
}

bool ScenePrefabAssetService::SaveInstancePrefab(Scene& scene, ScenePrefabInstanceHandle handle, const std::filesystem::path& path) {
    SceneState& state = SceneAccess::State(scene);
    const ScenePrefabInstanceRecord* instance = state.prefabInstances.Find(handle);
    return instance != nullptr && Save(scene, instance->prefab, path);
}

} // namespace kb::scene
