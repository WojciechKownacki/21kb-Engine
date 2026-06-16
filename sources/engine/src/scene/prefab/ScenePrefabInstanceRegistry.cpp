#include "scene/prefab/ScenePrefabInstanceRegistry.hpp"

#include <algorithm>
#include <utility>

namespace kb::scene {

ScenePrefabInstanceHandle ScenePrefabInstanceRegistry::Register(ScenePrefabHandle prefab, SceneObject rootParent, std::vector<SceneObject> objects, ScenePrefab resolvedPrefab) {
    if (!prefab.IsValid() || objects.empty()) {
        return {};
    }

    const std::uint64_t id = nextId_++;
    auto [iterator, inserted] = records_.emplace(
        id,
        ScenePrefabInstanceRecord{
            .prefab = prefab,
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
    nodeIndex = 0;
    if (!object.IsValid()) {
        return {};
    }

    const auto iterator = objectIndex_.find(object.Entity().Id());
    if (iterator == objectIndex_.end()) {
        return {};
    }
    nodeIndex = iterator->second.nodeIndex;
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
    IndexObjects(handle, record->objects);
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
    nextId_ = 1;
}

void ScenePrefabInstanceRegistry::IndexRecord(ScenePrefabInstanceHandle handle, const ScenePrefabInstanceRecord& record) {
    prefabIndex_[record.prefab].push_back(handle);
    IndexObjects(handle, record.objects);
}

void ScenePrefabInstanceRegistry::IndexObjects(ScenePrefabInstanceHandle handle, std::span<const SceneObject> objects) {
    if (!handle.IsValid()) {
        return;
    }

    if (!objects.empty() && objects.front().IsValid()) {
        rootIndex_[objects.front().Entity().Id()] = handle;
    }

    for (std::uint32_t nodeIndex = 0; nodeIndex < static_cast<std::uint32_t>(objects.size()); ++nodeIndex) {
        const SceneObject object = objects[nodeIndex];
        if (object.IsValid()) {
            objectIndex_[object.Entity().Id()] = ObjectIndexEntry{
                .instance = handle,
                .nodeIndex = nodeIndex,
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
