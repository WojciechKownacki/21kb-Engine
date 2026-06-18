#include "scene/prefab/ScenePrefabInstanceRegistry.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

namespace kb::scene {
namespace {

[[nodiscard]] std::vector<std::uint64_t> NodeIdsFor(const ScenePrefab& prefab) {
    const std::span<const ScenePrefabNodeDesc> nodes = prefab.Nodes();
    std::vector<std::uint64_t> nodeIds;
    nodeIds.reserve(nodes.size());
    for (const ScenePrefabNodeDesc& node : nodes) {
        nodeIds.push_back(node.stableId);
    }
    return nodeIds;
}

[[nodiscard]] std::uint32_t DenseIndex(SceneObject object) noexcept {
    return object.IsValid() ? kb::ecs::GeneratedEntityIndex(object.Entity()) : kb::ecs::kInvalidGeneratedEntityIndex;
}

} // namespace

ScenePrefabInstanceHandle ScenePrefabInstanceRegistry::Register(ScenePrefabHandle prefab, std::string prefabGuid, SceneObject rootParent, std::vector<SceneObject> objects, ScenePrefab resolvedPrefab) {
    if (!prefab.IsValid() || objects.empty()) {
        return {};
    }

    const std::uint64_t id = nextId_++;
    const ScenePrefabInstanceHandle handle{ id };
    EnsureRecordSlot(handle);
    const std::size_t slot = RecordSlotIndex(handle);
    records_[slot] = ScenePrefabInstanceRecord{
        .prefab = prefab,
        .prefabGuid = std::move(prefabGuid),
        .rootParent = rootParent,
        .objects = std::move(objects),
        .nodeIds = NodeIdsFor(resolvedPrefab),
        .resolvedPrefab = std::move(resolvedPrefab),
    };
    if (recordAlive_[slot] == 0U) {
        recordAlive_[slot] = 1U;
        ++liveRecordCount_;
    }
    IndexRecord(handle, records_[slot]);
    return handle;
}

bool ScenePrefabInstanceRegistry::Restore(
    ScenePrefabInstanceHandle handle,
    ScenePrefabHandle prefab,
    std::string prefabGuid,
    SceneObject rootParent,
    std::vector<SceneObject> objects,
    ScenePrefab resolvedPrefab,
    std::span<const std::uint64_t> nodeIds) {
    if (!handle.IsValid() || !prefab.IsValid() || objects.empty()) {
        return false;
    }

    static_cast<void>(Remove(handle));
    EnsureRecordSlot(handle);
    const std::size_t slot = RecordSlotIndex(handle);
    records_[slot] = ScenePrefabInstanceRecord{
        .prefab = prefab,
        .prefabGuid = std::move(prefabGuid),
        .rootParent = rootParent,
        .objects = std::move(objects),
        .nodeIds = nodeIds.empty() ? NodeIdsFor(resolvedPrefab) : std::vector<std::uint64_t>{ nodeIds.begin(), nodeIds.end() },
        .resolvedPrefab = std::move(resolvedPrefab),
    };
    if (recordAlive_[slot] == 0U) {
        recordAlive_[slot] = 1U;
        ++liveRecordCount_;
    }

    nextId_ = std::max(nextId_, handle.id_ + 1U);
    IndexRecord(handle, records_[slot]);
    return true;
}

std::vector<ScenePrefabInstanceHandle> ScenePrefabInstanceRegistry::RegisterMany(
    ScenePrefabHandle prefab,
    std::string_view prefabGuid,
    SceneObject rootParent,
    std::span<const std::vector<SceneObject>> objectSets,
    const ScenePrefab& resolvedPrefab,
    bool storeResolvedPrefab) {
    if (!prefab.IsValid() || objectSets.empty()) {
        return {};
    }

    for (const std::vector<SceneObject>& objects : objectSets) {
        if (objects.empty()) {
            return {};
        }
    }

    std::vector<ScenePrefabInstanceHandle> handles;
    handles.reserve(objectSets.size());
    records_.reserve(records_.size() + objectSets.size());
    recordAlive_.reserve(recordAlive_.size() + objectSets.size());
    const std::uint64_t firstBatchId = nextId_;
    EnsureRecordSlots(firstBatchId, objectSets.size());
    std::uint32_t maxDenseIndex = kb::ecs::kInvalidGeneratedEntityIndex;
    std::size_t sparseRootCount = 0;
    std::size_t sparseObjectCount = 0;
    bool contiguousDenseObjectRuns = true;
    for (const std::vector<SceneObject>& objects : objectSets) {
        const std::uint32_t rootDenseIndex = DenseIndex(objects.front());
        if (rootDenseIndex == kb::ecs::kInvalidGeneratedEntityIndex) {
            ++sparseRootCount;
            contiguousDenseObjectRuns = false;
        }
        for (std::size_t nodeIndex = 0; nodeIndex < objects.size(); ++nodeIndex) {
            const SceneObject object = objects[nodeIndex];
            const std::uint32_t denseIndex = DenseIndex(object);
            if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
                maxDenseIndex = maxDenseIndex == kb::ecs::kInvalidGeneratedEntityIndex ? denseIndex : std::max(maxDenseIndex, denseIndex);
                if (contiguousDenseObjectRuns && denseIndex != rootDenseIndex + nodeIndex) {
                    contiguousDenseObjectRuns = false;
                }
            } else if (object.IsValid()) {
                ++sparseObjectCount;
                contiguousDenseObjectRuns = false;
            } else {
                contiguousDenseObjectRuns = false;
            }
        }
    }
    if (sparseRootCount != 0U) {
        rootIndex_.reserve(rootIndex_.size() + sparseRootCount);
    }
    if (sparseObjectCount != 0U) {
        objectIndex_.reserve(objectIndex_.size() + sparseObjectCount);
    }
    if (maxDenseIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
        const std::size_t required = static_cast<std::size_t>(maxDenseIndex) + 1U;
        if (denseRootIndex_.size() < required) {
            denseRootIndex_.resize(required);
        }
        if (denseObjectIndex_.size() < required) {
            denseObjectIndex_.resize(required);
        }
    }
    const bool denseOnlyBatch = sparseRootCount == 0U && sparseObjectCount == 0U;
    const bool contiguousDenseBatch = denseOnlyBatch && contiguousDenseObjectRuns;
    std::vector<ScenePrefabInstanceHandle>& prefabHandles = prefabIndex_[prefab];
    prefabHandles.reserve(prefabHandles.size() + objectSets.size());
    auto sharedNodeIds = std::make_shared<std::vector<std::uint64_t>>(NodeIdsFor(resolvedPrefab));
    const std::vector<std::uint64_t>* pooledNodeIds = sharedNodeIds.get();
    batchNodeIdPool_.push_back(std::move(sharedNodeIds));
    std::shared_ptr<const ScenePrefab> sharedResolvedPrefab;
    const ScenePrefab* pooledResolvedPrefab = nullptr;
    if (storeResolvedPrefab) {
        sharedResolvedPrefab = std::make_shared<ScenePrefab>(resolvedPrefab);
        pooledResolvedPrefab = sharedResolvedPrefab.get();
        batchResolvedPrefabPool_.push_back(std::move(sharedResolvedPrefab));
    }
    auto sharedPrefabGuid = std::make_shared<std::string>(prefabGuid);
    const std::string* pooledPrefabGuid = sharedPrefabGuid.get();
    batchPrefabGuidPool_.push_back(std::move(sharedPrefabGuid));

    for (const std::vector<SceneObject>& objects : objectSets) {
        const std::uint64_t id = nextId_++;
        const ScenePrefabInstanceHandle handle{ id };
        const std::size_t slot = RecordSlotIndex(handle);
        ScenePrefabInstanceRecord& record = records_[slot];
        record.prefab = prefab;
        record.pooledPrefabGuid = pooledPrefabGuid;
        record.rootParent = rootParent;
        record.objects = objects;
        record.pooledNodeIds = pooledNodeIds;
        record.pooledResolvedPrefab = pooledResolvedPrefab;
        recordAlive_[slot] = 1U;
        ++liveRecordCount_;
        prefabHandles.push_back(handle);
        if (contiguousDenseBatch) {
            IndexContiguousDensePreparedObjects(handle, record);
        } else if (denseOnlyBatch) {
            IndexDensePreparedObjects(handle, record);
        } else {
            IndexObjects(handle, record);
        }
        handles.push_back(handle);
    }
    return handles;
}

std::vector<ScenePrefabInstanceHandle> ScenePrefabInstanceRegistry::RegisterManyInstances(
    ScenePrefabHandle prefab,
    std::string_view prefabGuid,
    SceneObject rootParent,
    std::span<const ScenePrefabInstance> instances,
    const ScenePrefab& resolvedPrefab,
    bool storeResolvedPrefab) {
    if (!prefab.IsValid() || instances.empty()) {
        return {};
    }

    for (const ScenePrefabInstance& instance : instances) {
        const std::span<const SceneObject> objects = instance.Objects();
        if (objects.empty()) {
            return {};
        }
    }

    std::vector<ScenePrefabInstanceHandle> handles;
    handles.reserve(instances.size());
    records_.reserve(records_.size() + instances.size());
    recordAlive_.reserve(recordAlive_.size() + instances.size());
    const std::uint64_t firstBatchId = nextId_;
    EnsureRecordSlots(firstBatchId, instances.size());
    std::uint32_t maxDenseIndex = kb::ecs::kInvalidGeneratedEntityIndex;
    std::size_t sparseRootCount = 0;
    std::size_t sparseObjectCount = 0;
    bool contiguousDenseObjectRuns = true;
    for (const ScenePrefabInstance& instance : instances) {
        const std::span<const SceneObject> objects = instance.Objects();
        const std::uint32_t rootDenseIndex = DenseIndex(objects.front());
        if (rootDenseIndex == kb::ecs::kInvalidGeneratedEntityIndex) {
            ++sparseRootCount;
            contiguousDenseObjectRuns = false;
        }
        for (std::size_t nodeIndex = 0; nodeIndex < objects.size(); ++nodeIndex) {
            const SceneObject object = objects[nodeIndex];
            const std::uint32_t denseIndex = DenseIndex(object);
            if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
                maxDenseIndex = maxDenseIndex == kb::ecs::kInvalidGeneratedEntityIndex ? denseIndex : std::max(maxDenseIndex, denseIndex);
                if (contiguousDenseObjectRuns && denseIndex != rootDenseIndex + nodeIndex) {
                    contiguousDenseObjectRuns = false;
                }
            } else if (object.IsValid()) {
                ++sparseObjectCount;
                contiguousDenseObjectRuns = false;
            } else {
                contiguousDenseObjectRuns = false;
            }
        }
    }
    if (sparseRootCount != 0U) {
        rootIndex_.reserve(rootIndex_.size() + sparseRootCount);
    }
    if (sparseObjectCount != 0U) {
        objectIndex_.reserve(objectIndex_.size() + sparseObjectCount);
    }
    if (maxDenseIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
        const std::size_t required = static_cast<std::size_t>(maxDenseIndex) + 1U;
        if (denseRootIndex_.size() < required) {
            denseRootIndex_.resize(required);
        }
        if (denseObjectIndex_.size() < required) {
            denseObjectIndex_.resize(required);
        }
    }
    const bool denseOnlyBatch = sparseRootCount == 0U && sparseObjectCount == 0U;
    const bool contiguousDenseBatch = denseOnlyBatch && contiguousDenseObjectRuns;

    std::vector<ScenePrefabInstanceHandle>& prefabHandles = prefabIndex_[prefab];
    prefabHandles.reserve(prefabHandles.size() + instances.size());
    auto sharedNodeIds = std::make_shared<std::vector<std::uint64_t>>(NodeIdsFor(resolvedPrefab));
    const std::vector<std::uint64_t>* pooledNodeIds = sharedNodeIds.get();
    batchNodeIdPool_.push_back(std::move(sharedNodeIds));
    std::shared_ptr<const ScenePrefab> sharedResolvedPrefab;
    const ScenePrefab* pooledResolvedPrefab = nullptr;
    if (storeResolvedPrefab) {
        sharedResolvedPrefab = std::make_shared<ScenePrefab>(resolvedPrefab);
        pooledResolvedPrefab = sharedResolvedPrefab.get();
        batchResolvedPrefabPool_.push_back(std::move(sharedResolvedPrefab));
    }
    auto sharedPrefabGuid = std::make_shared<std::string>(prefabGuid);
    const std::string* pooledPrefabGuid = sharedPrefabGuid.get();
    batchPrefabGuidPool_.push_back(std::move(sharedPrefabGuid));

    for (const ScenePrefabInstance& instance : instances) {
        const std::span<const SceneObject> objects = instance.Objects();
        const std::shared_ptr<const std::vector<SceneObject>> sharedObjects = instance.SharedObjects();
        const std::uint64_t id = nextId_++;
        const ScenePrefabInstanceHandle handle{ id };
        const std::size_t slot = RecordSlotIndex(handle);
        ScenePrefabInstanceRecord& record = records_[slot];
        record.prefab = prefab;
        record.pooledPrefabGuid = pooledPrefabGuid;
        record.rootParent = rootParent;
        record.sharedObjects = sharedObjects;
        record.pooledNodeIds = pooledNodeIds;
        record.pooledResolvedPrefab = pooledResolvedPrefab;
        recordAlive_[slot] = 1U;
        ++liveRecordCount_;
        prefabHandles.push_back(handle);
        if (contiguousDenseBatch) {
            IndexContiguousDensePreparedObjects(handle, record);
        } else if (denseOnlyBatch) {
            IndexDensePreparedObjects(handle, record);
        } else {
            IndexObjects(handle, record);
        }
        handles.push_back(handle);
    }
    return handles;
}

bool ScenePrefabInstanceRegistry::Contains(ScenePrefabInstanceHandle handle) const noexcept {
    return RecordSlotAlive(handle);
}

const ScenePrefabInstanceRecord* ScenePrefabInstanceRegistry::Find(ScenePrefabInstanceHandle handle) const noexcept {
    if (!RecordSlotAlive(handle)) {
        return nullptr;
    }

    return &records_[RecordSlotIndex(handle)];
}

ScenePrefabInstanceRecord* ScenePrefabInstanceRegistry::FindMutable(ScenePrefabInstanceHandle handle) noexcept {
    if (!RecordSlotAlive(handle)) {
        return nullptr;
    }

    return &records_[RecordSlotIndex(handle)];
}

ScenePrefabInstanceHandle ScenePrefabInstanceRegistry::FindRootInstance(SceneObject object) const noexcept {
    if (!object.IsValid()) {
        return {};
    }

    const std::uint32_t denseIndex = DenseIndex(object);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < denseRootIndex_.size()) {
        return denseRootIndex_[denseIndex];
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

    const std::uint32_t denseIndex = DenseIndex(object);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < denseObjectIndex_.size()) {
        const ObjectIndexEntry& entry = denseObjectIndex_[denseIndex];
        if (entry.instance.IsValid()) {
            nodeIndex = entry.nodeIndex;
            nodeId = NodeIdFor(entry.instance, entry.nodeIndex);
            return entry.instance;
        }
        return {};
    }

    const auto iterator = objectIndex_.find(object.Entity().Id());
    if (iterator == objectIndex_.end()) {
        return {};
    }
    nodeIndex = iterator->second.nodeIndex;
    nodeId = NodeIdFor(iterator->second.instance, iterator->second.nodeIndex);
    return iterator->second.instance;
}

std::vector<ScenePrefabInstanceHandle> ScenePrefabInstanceRegistry::Handles() const {
    std::vector<ScenePrefabInstanceHandle> handles;
    handles.reserve(liveRecordCount_);
    for (std::size_t index = 0; index < recordAlive_.size(); ++index) {
        if (recordAlive_[index] != 0U) {
            handles.push_back(ScenePrefabInstanceHandle{ static_cast<std::uint64_t>(index) + 1U });
        }
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
    return liveRecordCount_;
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
    record->pooledPrefabGuid = nullptr;
    record->sharedPrefabGuid.reset();
    return true;
}

bool ScenePrefabInstanceRegistry::Remove(ScenePrefabInstanceHandle handle) noexcept {
    if (!RecordSlotAlive(handle)) {
        return false;
    }

    const std::size_t slot = RecordSlotIndex(handle);
    ScenePrefabInstanceRecord& record = records_[slot];
    RemoveFromPrefabIndex(record.prefab, handle);
    UnindexObjects(handle, record.Objects());
    record = ScenePrefabInstanceRecord{};
    recordAlive_[slot] = 0U;
    --liveRecordCount_;
    if (liveRecordCount_ == 0U) {
        batchPrefabGuidPool_.clear();
        batchNodeIdPool_.clear();
        batchResolvedPrefabPool_.clear();
    }
    return true;
}

void ScenePrefabInstanceRegistry::Clear() noexcept {
    records_.clear();
    recordAlive_.clear();
    liveRecordCount_ = 0;
    prefabIndex_.clear();
    denseRootIndex_.clear();
    denseObjectIndex_.clear();
    rootIndex_.clear();
    objectIndex_.clear();
    batchPrefabGuidPool_.clear();
    batchNodeIdPool_.clear();
    batchResolvedPrefabPool_.clear();
}

void ScenePrefabInstanceRegistry::IndexRecord(ScenePrefabInstanceHandle handle, const ScenePrefabInstanceRecord& record) {
    prefabIndex_[record.prefab].push_back(handle);
    IndexObjects(handle, record);
}

void ScenePrefabInstanceRegistry::IndexObjects(ScenePrefabInstanceHandle handle, const ScenePrefabInstanceRecord& record) {
    if (!handle.IsValid()) {
        return;
    }

    const std::span<const SceneObject> objects = record.Objects();
    if (!objects.empty() && objects.front().IsValid()) {
        const std::uint32_t denseIndex = DenseIndex(objects.front());
        if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
            if (denseRootIndex_.size() <= denseIndex) {
                denseRootIndex_.resize(static_cast<std::size_t>(denseIndex) + 1U);
            }
            denseRootIndex_[denseIndex] = handle;
        } else {
            rootIndex_[objects.front().Entity().Id()] = handle;
        }
    }

    for (std::uint32_t nodeIndex = 0; nodeIndex < static_cast<std::uint32_t>(objects.size()); ++nodeIndex) {
        const SceneObject object = objects[nodeIndex];
        if (object.IsValid()) {
            const ObjectIndexEntry entry{
                .instance = handle,
                .nodeIndex = nodeIndex,
            };
            const std::uint32_t denseIndex = DenseIndex(object);
            if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
                if (denseObjectIndex_.size() <= denseIndex) {
                    denseObjectIndex_.resize(static_cast<std::size_t>(denseIndex) + 1U);
                }
                denseObjectIndex_[denseIndex] = entry;
            } else {
                objectIndex_[object.Entity().Id()] = entry;
            }
        }
    }
}

void ScenePrefabInstanceRegistry::IndexContiguousDensePreparedObjects(ScenePrefabInstanceHandle handle, const ScenePrefabInstanceRecord& record) noexcept {
    if (!handle.IsValid()) {
        return;
    }

    const std::span<const SceneObject> objects = record.Objects();
    if (objects.empty() || !objects.front().IsValid()) {
        return;
    }

    const std::uint32_t firstDenseIndex = DenseIndex(objects.front());
    if (firstDenseIndex == kb::ecs::kInvalidGeneratedEntityIndex) {
        return;
    }

    const std::size_t firstIndex = static_cast<std::size_t>(firstDenseIndex);
    if (firstIndex >= denseRootIndex_.size() || firstIndex + objects.size() > denseObjectIndex_.size()) {
        return;
    }

    denseRootIndex_[firstIndex] = handle;
    for (std::uint32_t nodeIndex = 0; nodeIndex < static_cast<std::uint32_t>(objects.size()); ++nodeIndex) {
        denseObjectIndex_[firstIndex + nodeIndex] = ObjectIndexEntry{
            .instance = handle,
            .nodeIndex = nodeIndex,
        };
    }
}

void ScenePrefabInstanceRegistry::IndexDensePreparedObjects(ScenePrefabInstanceHandle handle, const ScenePrefabInstanceRecord& record) noexcept {
    if (!handle.IsValid()) {
        return;
    }

    const std::span<const SceneObject> objects = record.Objects();
    if (objects.empty()) {
        return;
    }

    if (objects.front().IsValid()) {
        const std::uint32_t rootDenseIndex = DenseIndex(objects.front());
        if (rootDenseIndex != kb::ecs::kInvalidGeneratedEntityIndex && rootDenseIndex < denseRootIndex_.size()) {
            denseRootIndex_[rootDenseIndex] = handle;
        }
    }

    for (std::uint32_t nodeIndex = 0; nodeIndex < static_cast<std::uint32_t>(objects.size()); ++nodeIndex) {
        const SceneObject object = objects[nodeIndex];
        if (!object.IsValid()) {
            continue;
        }

        const std::uint32_t denseIndex = DenseIndex(object);
        if (denseIndex == kb::ecs::kInvalidGeneratedEntityIndex || denseIndex >= denseObjectIndex_.size()) {
            continue;
        }

        denseObjectIndex_[denseIndex] = ObjectIndexEntry{
            .instance = handle,
            .nodeIndex = nodeIndex,
        };
    }
}

void ScenePrefabInstanceRegistry::UnindexObjects(ScenePrefabInstanceHandle handle, std::span<const SceneObject> objects) noexcept {
    if (!objects.empty() && objects.front().Entity().IsValid()) {
        const std::uint32_t denseIndex = DenseIndex(objects.front());
        if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < denseRootIndex_.size()) {
            if (denseRootIndex_[denseIndex] == handle) {
                denseRootIndex_[denseIndex] = {};
            }
        } else {
            const auto rootIterator = rootIndex_.find(objects.front().Entity().Id());
            if (rootIterator != rootIndex_.end() && rootIterator->second == handle) {
                rootIndex_.erase(rootIterator);
            }
        }
    }

    for (std::uint32_t nodeIndex = 0; nodeIndex < static_cast<std::uint32_t>(objects.size()); ++nodeIndex) {
        const SceneObject object = objects[nodeIndex];
        if (!object.Entity().IsValid()) {
            continue;
        }

        const std::uint32_t denseIndex = DenseIndex(object);
        if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < denseObjectIndex_.size()) {
            if (denseObjectIndex_[denseIndex].instance == handle) {
                denseObjectIndex_[denseIndex] = {};
            }
        } else {
            const auto objectIterator = objectIndex_.find(object.Entity().Id());
            if (objectIterator != objectIndex_.end() && objectIterator->second.instance == handle) {
                objectIndex_.erase(objectIterator);
            }
        }
    }
}

void ScenePrefabInstanceRegistry::EnsureRecordSlot(ScenePrefabInstanceHandle handle) {
    const std::size_t slot = RecordSlotIndex(handle);
    if (records_.size() <= slot) {
        records_.resize(slot + 1U);
        recordAlive_.resize(slot + 1U, 0U);
    }
}

void ScenePrefabInstanceRegistry::EnsureRecordSlots(std::uint64_t firstId, std::size_t count) {
    if (firstId == 0U || count == 0U) {
        return;
    }

    const std::uint64_t lastId = firstId + static_cast<std::uint64_t>(count) - 1U;
    const std::size_t lastSlot = static_cast<std::size_t>(lastId - 1U);
    if (records_.size() <= lastSlot) {
        records_.resize(lastSlot + 1U);
        recordAlive_.resize(lastSlot + 1U, 0U);
    }
}

std::uint64_t ScenePrefabInstanceRegistry::NodeIdFor(ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex) const noexcept {
    if (!RecordSlotAlive(handle)) {
        return ScenePrefabNodeDesc::InvalidStableId;
    }

    const ScenePrefabInstanceRecord& record = records_[RecordSlotIndex(handle)];
    const std::span<const std::uint64_t> nodeIds = record.NodeIds();
    return nodeIndex < nodeIds.size() ? nodeIds[nodeIndex] : ScenePrefabNodeDesc::InvalidStableId;
}

bool ScenePrefabInstanceRegistry::RecordSlotAlive(ScenePrefabInstanceHandle handle) const noexcept {
    if (!handle.IsValid()) {
        return false;
    }
    const std::size_t slot = RecordSlotIndex(handle);
    return slot < recordAlive_.size() && recordAlive_[slot] != 0U;
}

std::size_t ScenePrefabInstanceRegistry::RecordSlotIndex(ScenePrefabInstanceHandle handle) noexcept {
    return static_cast<std::size_t>(handle.id_ - 1U);
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
