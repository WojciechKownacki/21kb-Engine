#include "engine/scene/ScenePrefabs.hpp"

#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/ScenePrefabPrivateScene.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabInstanceRegistry.hpp"
#include "scene/prefab/ScenePrefabInstanceSynchronizer.hpp"
#include "scene/prefab/ScenePrefabRecord.hpp"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace kb::scene {
namespace {

[[nodiscard]] ScenePrefabAssetType AssetTypeForRecord(const ScenePrefabRecord* record) noexcept {
    if (record == nullptr) {
        return ScenePrefabAssetType::Missing;
    }
    return record->kind == ScenePrefabRecordKind::Variant ? ScenePrefabAssetType::Variant : ScenePrefabAssetType::Template;
}

[[nodiscard]] ScenePrefabHandle OriginalSource(ScenePrefabRegistry& registry, ScenePrefabHandle handle) noexcept {
    ScenePrefabHandle current = handle;
    while (current.IsValid()) {
        const ScenePrefabRecord* record = registry.FindRecord(current);
        if (record == nullptr) {
            return {};
        }
        if (record->kind != ScenePrefabRecordKind::Variant) {
            return current;
        }
        current = record->basePrefab;
    }
    return {};
}

void CollectSubtreeEntities(Scene& scene, SceneEntity entity, std::unordered_set<SceneEntity::IdType>& entities) {
    if (!entity.IsValid() || !scene.Entities().IsAlive(entity)) {
        return;
    }

    entities.insert(entity.Id());
    for (const SceneEntity child : scene.Hierarchy().ChildEntities(entity)) {
        CollectSubtreeEntities(scene, child, entities);
    }
}

[[nodiscard]] bool RemoveCompleteInstanceLinks(Scene& scene, ScenePrefabInstanceHandle handle) {
    SceneState& state = SceneAccess::State(scene);
    const ScenePrefabInstanceRecord* record = state.prefabInstances.Find(handle);
    if (record == nullptr) {
        return false;
    }

    std::unordered_set<SceneEntity::IdType> subtree;
    if (!record->objects.empty()) {
        CollectSubtreeEntities(scene, record->objects.front().Entity(), subtree);
    }

    bool removed = false;
    const std::vector<ScenePrefabInstanceHandle> handles = state.prefabInstances.Handles();
    for (const ScenePrefabInstanceHandle candidate : handles) {
        const ScenePrefabInstanceRecord* candidateRecord = state.prefabInstances.Find(candidate);
        if (candidateRecord == nullptr || candidateRecord->objects.empty()) {
            continue;
        }
        if (candidate == handle || subtree.contains(candidateRecord->objects.front().Entity().Id())) {
            removed = state.prefabInstances.Remove(candidate) || removed;
        }
    }
    return removed;
}

} // namespace

ScenePrefabAssetType ScenePrefabs::AssetType(ScenePrefabHandle handle) const noexcept {
    if (!handle.IsValid()) {
        return ScenePrefabAssetType::None;
    }
    return AssetTypeForRecord(SceneAccess::State(scene_).prefabs.FindRecord(handle));
}

ScenePrefabInstanceStatus ScenePrefabs::InstanceStatus(ScenePrefabInstanceHandle handle) const noexcept {
    if (!handle.IsValid()) {
        return ScenePrefabInstanceStatus::NotInstance;
    }

    const SceneState& state = SceneAccess::State(scene_);
    const ScenePrefabInstanceRecord* instance = state.prefabInstances.Find(handle);
    if (instance == nullptr) {
        return ScenePrefabInstanceStatus::NotInstance;
    }
    return state.prefabs.FindRecord(instance->prefab) == nullptr ? ScenePrefabInstanceStatus::MissingAsset : ScenePrefabInstanceStatus::Connected;
}

ScenePrefabHandle ScenePrefabs::SourcePrefab(ScenePrefabInstanceHandle handle) const noexcept {
    const ScenePrefabInstanceRecord* instance = SceneAccess::State(scene_).prefabInstances.Find(handle);
    return instance == nullptr ? ScenePrefabHandle{} : instance->prefab;
}

ScenePrefabHandle ScenePrefabs::SourcePrefab(SceneObject object) const noexcept {
    std::uint32_t nodeIndex = 0;
    return SourcePrefab(ContainingInstance(object, nodeIndex));
}

ScenePrefabHandle ScenePrefabs::SourcePrefab(SceneEntity entity) const noexcept {
    return SourcePrefab(SceneAccess::MakeObject(scene_, entity));
}

ScenePrefabHandle ScenePrefabs::OriginalSourcePrefab(ScenePrefabHandle handle) const noexcept {
    return OriginalSource(SceneAccess::State(scene_).prefabs, handle);
}

ScenePrefabHandle ScenePrefabs::OriginalSourcePrefab(ScenePrefabInstanceHandle handle) const noexcept {
    return OriginalSourcePrefab(SourcePrefab(handle));
}

ScenePrefabHandle ScenePrefabs::OriginalSourcePrefab(SceneObject object) const noexcept {
    std::uint32_t nodeIndex = 0;
    return OriginalSourcePrefab(ContainingInstance(object, nodeIndex));
}

ScenePrefabHandle ScenePrefabs::OriginalSourcePrefab(SceneEntity entity) const noexcept {
    return OriginalSourcePrefab(SceneAccess::MakeObject(scene_, entity));
}

ScenePrefabPrivateScene ScenePrefabs::OpenPrivateScene(ScenePrefabHandle handle) {
    SceneState& state = SceneAccess::State(scene_);
    const ScenePrefab* prefab = state.prefabs.Find(handle);
    if (prefab == nullptr) {
        return {};
    }

    auto editScene = std::make_unique<Scene>(SceneMode::PrefabPrivate);
    const ScenePrefabHandle editHandle = editScene->Prefabs().Register("PrivatePrefabEdit", *prefab);
    if (!editHandle.IsValid()) {
        return {};
    }

    ScenePrefabInstance editInstance = editScene->Prefabs().Instantiate(editHandle);
    if (!editInstance.Handle().IsValid()) {
        return {};
    }

    return ScenePrefabPrivateScene{ scene_, handle, std::move(editScene), editHandle, std::move(editInstance) };
}

std::size_t ScenePrefabs::RefreshInstances(ScenePrefabHandle handle) {
    return ScenePrefabInstanceSynchronizer::Refresh(scene_, handle);
}

bool ScenePrefabs::Reconnect(ScenePrefabInstanceHandle handle, ScenePrefabHandle sourcePrefab) {
    if (!handle.IsValid() || !sourcePrefab.IsValid()) {
        return false;
    }

    SceneState& state = SceneAccess::State(scene_);
    ScenePrefabInstanceRecord* instance = state.prefabInstances.FindMutable(handle);
    const ScenePrefabRecord* source = state.prefabs.FindRecord(sourcePrefab);
    if (instance == nullptr || source == nullptr) {
        return false;
    }
    if (!instance->prefabGuid.empty() && instance->prefabGuid != source->guid) {
        return false;
    }

    const ScenePrefabHandle previousPrefab = instance->prefab;
    const std::string previousGuid = instance->prefabGuid;
    const ScenePrefab previousResolvedPrefab = instance->resolvedPrefab;
    const std::vector<SceneObject> oldObjects = instance->objects;

    if (!state.prefabInstances.UpdateSource(handle, sourcePrefab, source->guid)) {
        return false;
    }

    instance = state.prefabInstances.FindMutable(handle);
    if (instance == nullptr) {
        return false;
    }

    if (ScenePrefabInstanceSynchronizer::RefreshInstance(scene_, state.prefabs, *instance)) {
        state.prefabInstances.ReindexObjects(handle, oldObjects);
        return true;
    }

    static_cast<void>(state.prefabInstances.UpdateSource(handle, previousPrefab, previousGuid));
    instance = state.prefabInstances.FindMutable(handle);
    if (instance != nullptr) {
        instance->resolvedPrefab = previousResolvedPrefab;
    }
    return false;
}

void ScenePrefabs::Clear() noexcept {
    SceneState& state = SceneAccess::State(scene_);
    state.prefabInstances.Clear();
    state.prefabs.Clear();
}

bool ScenePrefabs::Unpack(ScenePrefabInstanceHandle handle, ScenePrefabUnpackMode mode) {
    if (!handle.IsValid()) {
        return false;
    }

    if (mode == ScenePrefabUnpackMode::Complete) {
        return RemoveCompleteInstanceLinks(scene_, handle);
    }
    return SceneAccess::State(scene_).prefabInstances.Remove(handle);
}

} // namespace kb::scene
