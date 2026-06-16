#include "scene/prefab/ScenePrefabInstanceRegistry.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace kb::scene {

ScenePrefabInstanceHandle ScenePrefabInstanceRegistry::Register(ScenePrefabHandle prefab, std::string prefabGuid, SceneObject rootParent, std::vector<SceneObject> objects, ScenePrefab resolvedPrefab) {
    if (!prefab.IsValid() || objects.empty()) {
        return {};
    }

    const std::uint64_t id = nextId_++;
    auto [iterator, inserted] = records_.emplace(
        id,
        ScenePrefabInstanceRecord{
            .prefab = prefab,
            .prefabGuid = std::move(prefabGuid),
            .rootParent = rootParent,
            .objects = std::move(objects),
            .resolvedPrefab = std::move(resolvedPrefab),
        });
    const ScenePrefabInstanceHandle handle{ id };
    if (inserted) {
        IndexRecord(handle, iterator->second);
    }
    return handle;
}

std::vector<ScenePrefabInstanceHandle> ScenePrefabInstanceRegistry::RegisterMany(
    ScenePrefabHandle prefab,
    std::string_view prefabGuid,
    SceneObject rootParent,
    std::span<const std::vector<SceneObject>> objectSets,
    const ScenePrefab& resolvedPrefab) {
    if (!prefab.IsValid() || objectSets.empty()) {
        return {};
    }

    std::size_t objectCount = 0;
    for (const std::vector<SceneObject>& objects : objectSets) {
        if (objects.empty()) {
            return {};
        }
        objectCount += objects.size();
    }

    std::vector<ScenePrefabInstanceHandle> handles;
    handles.reserve(objectSets.size());
    records_.reserve(records_.size() + objectSets.size());
    rootIndex_.reserve(rootIndex_.size() + objectSets.size());
    objectIndex_.reserve(objectIndex_.size() + objectCount);
    std::vector<ScenePrefabInstanceHandle>& prefabHandles = prefabIndex_[prefab];
    prefabHandles.reserve(prefabHandles.size() + objectSets.size());

    for (const std::vector<SceneObject>& objects : objectSets) {
        const std::uint64_t id = nextId_++;
        auto [iterator, inserted] = records_.emplace(
            id,
            ScenePrefabInstanceRecord{
                .prefab = prefab,
                .prefabGuid = std::string{ prefabGuid },
                .rootParent = rootParent,
                .objects = objects,
                .resolvedPrefab = resolvedPrefab,
            });
        if (!inserted) {
            continue;
        }

        const ScenePrefabInstanceHandle handle{ id };
        prefabHandles.push_back(handle);
        IndexObjects(handle, iterator->second);
        handles.push_back(handle);
    }
    return handles;
}

bool ScenePrefabInstanceRegistry::Contains(ScenePrefabInstanceHandle handle) const noexcept {
    return handle.IsValid() && records_.contains(handle.id_);
}

const ScenePrefabInstanceRecord* ScenePrefabInstanceRegistry::Find(ScenePrefabInstanceHandle handle) const noexcept {
    if (!handle.IsValid()) {
        return nullptr;
    }

    const auto iterator = records_.find(handle.id_);
    return iterator == records_.end() ? nullptr : &iterator->second;
}

ScenePrefabInstanceRecord* ScenePrefabInstanceRegistry::FindMutable(ScenePrefabInstanceHandle handle) noexcept {
    if (!handle.IsValid()) {
        return nullptr;
    }

    const auto iterator = records_.find(handle.id_);
    return iterator == records_.end() ? nullptr : &iterator->second;
}

ScenePrefabInstanceHandle ScenePrefabInstanceRegistry::FindRootInstance(SceneObject object) const noexcept {
    if (!object.IsValid()) {
        return {};
    }

    const auto iterator = rootIndex_.find(object.Entity().Id());
    return iterator == rootIndex_.end() ? ScenePrefabInstanceHandle{} : iterator->second;
}

ScenePrefabInstanceHandle ScenePrefabInstanceRegistry::FindContainingInstance(SceneObject object, std::uint32_t& nodeIndex) const noexcept {
    std::uint64_t nodeId = ScenePrefabNodeDesc::InvalidStableId;
    return FindContainingInstance(object, nodeIndex, nodeId);
}

ScenePrefabInstanceHandle ScenePrefabInstanceRegistry::FindContainingInstance(SceneObject object, std::uint32_t& nodeIndex, std::uint64_t& nodeId) const noexcept {
    nodeIndex = 0;
    nodeId = ScenePrefabNodeDesc::InvalidStableId;
    if (!object.IsValid()) {
        return {};
    }

    const auto iterator = objectIndex_.find(object.Entity().Id());
    if (iterator == objectIndex_.end()) {
        return {};
    }
    nodeIndex = iterator->second.nodeIndex;
    nodeId = iterator->second.nodeId;
    return iterator->second.instance;
}

std::vector<ScenePrefabInstanceHandle> ScenePrefabInstanceRegistry::Handles() const {
    std::vector<ScenePrefabInstanceHandle> handles;
    handles.reserve(records_.size());
    for (const auto& [id, record] : records_) {
        static_cast<void>(record);
        handles.push_back(ScenePrefabInstanceHandle{ id });
    }
    return handles;
}

std::vector<ScenePrefabInstanceHandle> ScenePrefabInstanceRegistry::HandlesForPrefab(ScenePrefabHandle prefab) const {
    if (!prefab.IsValid()) {
        return {};
    }

    const auto iterator = prefabIndex_.find(prefab);
    return iterator == prefabIndex_.end() ? std::vector<ScenePrefabInstanceHandle>{} : iterator->second;
}

std::size_t ScenePrefabInstanceRegistry::Count() const noexcept {
    return records_.size();
}

void ScenePrefabInstanceRegistry::ReindexObjects(ScenePrefabInstanceHandle handle, std::span<const SceneObject> oldObjects) noexcept {
    ScenePrefabInstanceRecord* record = FindMutable(handle);
    if (record == nullptr) {
        return;
    }

    UnindexObjects(handle, oldObjects);
    IndexObjects(handle, *record);
}

bool ScenePrefabInstanceRegistry::UpdateSource(ScenePrefabInstanceHandle handle, ScenePrefabHandle prefab, std::string prefabGuid) {
    if (!handle.IsValid() || !prefab.IsValid()) {
        return false;
    }

    ScenePrefabInstanceRecord* record = FindMutable(handle);
    if (record == nullptr) {
        return false;
    }

    if (record->prefab != prefab) {
        RemoveFromPrefabIndex(record->prefab, handle);
        prefabIndex_[prefab].push_back(handle);
    }
    record->prefab = prefab;
    record->prefabGuid = std::move(prefabGuid);
    return true;
}

bool ScenePrefabInstanceRegistry::Remove(ScenePrefabInstanceHandle handle) noexcept {
    if (!handle.IsValid()) {
        return false;
    }
    const auto iterator = records_.find(handle.id_);
    if (iterator == records_.end()) {
        return false;
    }

    RemoveFromPrefabIndex(iterator->second.prefab, handle);
    UnindexObjects(handle, iterator->second.objects);
    records_.erase(iterator);
    return true;
}

void ScenePrefabInstanceRegistry::Clear() noexcept {
    records_.clear();
    prefabIndex_.clear();
    rootIndex_.clear();
    objectIndex_.clear();
}

void ScenePrefabInstanceRegistry::IndexRecord(ScenePrefabInstanceHandle handle, const ScenePrefabInstanceRecord& record) {
    prefabIndex_[record.prefab].push_back(handle);
    IndexObjects(handle, record);
}

void ScenePrefabInstanceRegistry::IndexObjects(ScenePrefabInstanceHandle handle, const ScenePrefabInstanceRecord& record) {
    if (!handle.IsValid()) {
        return;
    }

    const std::span<const SceneObject> objects = record.objects;
    const std::span<const ScenePrefabNodeDesc> nodes = record.resolvedPrefab.Nodes();
    if (!objects.empty() && objects.front().IsValid()) {
        rootIndex_[objects.front().Entity().Id()] = handle;
    }

    for (std::uint32_t nodeIndex = 0; nodeIndex < static_cast<std::uint32_t>(objects.size()); ++nodeIndex) {
        const SceneObject object = objects[nodeIndex];
        if (object.IsValid()) {
            objectIndex_[object.Entity().Id()] = ObjectIndexEntry{
                .instance = handle,
                .nodeIndex = nodeIndex,
                .nodeId = nodeIndex < nodes.size() ? nodes[nodeIndex].stableId : ScenePrefabNodeDesc::InvalidStableId,
            };
        }
    }
}

void ScenePrefabInstanceRegistry::UnindexObjects(ScenePrefabInstanceHandle handle, std::span<const SceneObject> objects) noexcept {
    if (!objects.empty() && objects.front().IsValid()) {
        const auto rootIterator = rootIndex_.find(objects.front().Entity().Id());
        if (rootIterator != rootIndex_.end() && rootIterator->second == handle) {
            rootIndex_.erase(rootIterator);
        }
    }

    for (std::uint32_t nodeIndex = 0; nodeIndex < static_cast<std::uint32_t>(objects.size()); ++nodeIndex) {
        const SceneObject object = objects[nodeIndex];
        if (!object.IsValid()) {
            continue;
        }

        const auto objectIterator = objectIndex_.find(object.Entity().Id());
        if (objectIterator != objectIndex_.end() && objectIterator->second.instance == handle) {
            objectIndex_.erase(objectIterator);
        }
    }
}

void ScenePrefabInstanceRegistry::RemoveFromPrefabIndex(ScenePrefabHandle prefab, ScenePrefabInstanceHandle handle) noexcept {
    const auto iterator = prefabIndex_.find(prefab);
    if (iterator == prefabIndex_.end()) {
        return;
    }

    std::vector<ScenePrefabInstanceHandle>& handles = iterator->second;
    handles.erase(std::remove(handles.begin(), handles.end(), handle), handles.end());
    if (handles.empty()) {
        prefabIndex_.erase(iterator);
    }
}

} // namespace kb::scene
