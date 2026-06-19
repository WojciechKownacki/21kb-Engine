#pragma once

#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabInstanceHandle.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefab.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kb::scene {

struct ScenePrefabInstanceRecord {
    ScenePrefabHandle prefab;
    std::string prefabGuid;
    const std::string* pooledPrefabGuid = nullptr;
    std::shared_ptr<const std::string> sharedPrefabGuid;
    SceneObject rootParent{};
    std::vector<SceneObject> objects;
    std::shared_ptr<const std::vector<SceneObject>> sharedObjects;
    std::shared_ptr<const std::vector<SceneObject>> sharedObjectSlab;
    const std::vector<SceneObject>* pooledObjectSlab = nullptr;
    std::size_t objectOffset = 0U;
    std::size_t objectCount = 0U;
    std::vector<std::uint64_t> nodeIds;
    std::vector<std::uint32_t> dirtyNodeIndices;
    bool topologyDirty = false;
    const std::vector<std::uint64_t>* pooledNodeIds = nullptr;
    std::shared_ptr<const std::vector<std::uint64_t>> sharedNodeIds;
    ScenePrefab resolvedPrefab;
    const ScenePrefab* pooledResolvedPrefab = nullptr;
    std::shared_ptr<const ScenePrefab> sharedResolvedPrefab;

    [[nodiscard]] std::string_view PrefabGuid() const noexcept {
        if (pooledPrefabGuid != nullptr) {
            return std::string_view{ *pooledPrefabGuid };
        }
        if (sharedPrefabGuid != nullptr) {
            return std::string_view{ *sharedPrefabGuid };
        }
        return std::string_view{ prefabGuid };
    }

    [[nodiscard]] const ScenePrefab* ResolvedPrefab() const noexcept {
        if (!resolvedPrefab.Empty()) {
            return &resolvedPrefab;
        }
        if (pooledResolvedPrefab != nullptr) {
            return pooledResolvedPrefab;
        }
        return sharedResolvedPrefab.get();
    }

    [[nodiscard]] std::span<const SceneObject> Objects() const noexcept {
        if (pooledObjectSlab != nullptr) {
            if (objectOffset > pooledObjectSlab->size()) {
                return {};
            }
            const std::size_t available = pooledObjectSlab->size() - objectOffset;
            const std::size_t count = std::min(objectCount, available);
            return std::span<const SceneObject>{ pooledObjectSlab->data() + objectOffset, count };
        }
        if (sharedObjectSlab != nullptr) {
            if (objectOffset > sharedObjectSlab->size()) {
                return {};
            }
            const std::size_t available = sharedObjectSlab->size() - objectOffset;
            const std::size_t count = std::min(objectCount, available);
            return std::span<const SceneObject>{ sharedObjectSlab->data() + objectOffset, count };
        }
        if (sharedObjects != nullptr) {
            return std::span<const SceneObject>{ *sharedObjects };
        }
        return std::span<const SceneObject>{ objects };
    }

    [[nodiscard]] std::vector<SceneObject>& MutableObjects() {
        if (pooledObjectSlab != nullptr) {
            const std::span<const SceneObject> readObjects = Objects();
            objects.assign(readObjects.begin(), readObjects.end());
            pooledObjectSlab = nullptr;
            objectOffset = 0U;
            objectCount = 0U;
            return objects;
        }
        if (sharedObjectSlab != nullptr) {
            const std::span<const SceneObject> readObjects = Objects();
            objects.assign(readObjects.begin(), readObjects.end());
            sharedObjectSlab.reset();
            objectOffset = 0U;
            objectCount = 0U;
            return objects;
        }
        if (sharedObjects != nullptr) {
            objects.assign(sharedObjects->begin(), sharedObjects->end());
            sharedObjects.reset();
        }
        return objects;
    }

    void SetObjects(std::vector<SceneObject> updatedObjects) {
        pooledObjectSlab = nullptr;
        sharedObjectSlab.reset();
        sharedObjects.reset();
        objectOffset = 0U;
        objectCount = 0U;
        objects = std::move(updatedObjects);
        dirtyNodeIndices.clear();
        topologyDirty = false;
    }

    [[nodiscard]] std::span<const std::uint64_t> NodeIds() const noexcept {
        if (pooledNodeIds != nullptr) {
            return std::span<const std::uint64_t>{ *pooledNodeIds };
        }
        if (sharedNodeIds != nullptr) {
            return std::span<const std::uint64_t>{ *sharedNodeIds };
        }
        return std::span<const std::uint64_t>{ nodeIds };
    }

    [[nodiscard]] const ScenePrefab& BaselineOr(const ScenePrefab& fallback) const noexcept {
        const ScenePrefab* resolved = ResolvedPrefab();
        return resolved == nullptr ? fallback : *resolved;
    }

    void SetResolvedPrefab(ScenePrefab resolved) {
        nodeIds.clear();
        nodeIds.reserve(resolved.NodeCount());
        for (const ScenePrefabNodeDesc& node : resolved.Nodes()) {
            nodeIds.push_back(node.stableId);
        }
        pooledNodeIds = nullptr;
        sharedNodeIds.reset();
        pooledResolvedPrefab = nullptr;
        sharedResolvedPrefab.reset();
        resolvedPrefab = std::move(resolved);
    }

    void SetSharedResolvedPrefab(std::shared_ptr<const ScenePrefab> resolved, std::shared_ptr<const std::vector<std::uint64_t>> resolvedNodeIds) {
        nodeIds.clear();
        pooledNodeIds = nullptr;
        sharedNodeIds = std::move(resolvedNodeIds);
        resolvedPrefab = {};
        pooledResolvedPrefab = nullptr;
        sharedResolvedPrefab = std::move(resolved);
    }
};

class ScenePrefabInstanceRegistry {
public:
    [[nodiscard]] ScenePrefabInstanceHandle Register(ScenePrefabHandle prefab, std::string prefabGuid, SceneObject rootParent, std::vector<SceneObject> objects, ScenePrefab resolvedPrefab);
    [[nodiscard]] bool Restore(
        ScenePrefabInstanceHandle handle,
        ScenePrefabHandle prefab,
        std::string prefabGuid,
        SceneObject rootParent,
        std::vector<SceneObject> objects,
        ScenePrefab resolvedPrefab,
        std::span<const std::uint64_t> nodeIds = {});
    [[nodiscard]] std::vector<ScenePrefabInstanceHandle> RegisterMany(
        ScenePrefabHandle prefab,
        std::string_view prefabGuid,
        SceneObject rootParent,
        std::span<const std::vector<SceneObject>> objectSets,
        const ScenePrefab& resolvedPrefab,
        bool storeResolvedPrefab = true);
    [[nodiscard]] std::vector<ScenePrefabInstanceHandle> RegisterManyInstances(
        ScenePrefabHandle prefab,
        std::string_view prefabGuid,
        SceneObject rootParent,
        std::span<const ScenePrefabInstance> instances,
        const ScenePrefab& resolvedPrefab,
        bool storeResolvedPrefab = true);
    [[nodiscard]] std::size_t RegisterManyInstancesInPlace(
        ScenePrefabHandle prefab,
        std::string_view prefabGuid,
        SceneObject rootParent,
        std::span<ScenePrefabInstance> instances,
        const ScenePrefab& resolvedPrefab,
        bool storeResolvedPrefab = true);
    [[nodiscard]] std::size_t RegisterManyCreatedDenseInstancesInPlace(
        ScenePrefabHandle prefab,
        std::string_view prefabGuid,
        SceneObject rootParent,
        std::span<ScenePrefabInstance> instances,
        const ScenePrefab& resolvedPrefab,
        std::uint32_t maxDenseIndex,
        bool trustedRegularSharedObjectSlab = false,
        bool trustedContiguousDenseObjectRuns = false,
        bool storeResolvedPrefab = true);
    [[nodiscard]] bool Contains(ScenePrefabInstanceHandle handle) const noexcept;
    [[nodiscard]] const ScenePrefabInstanceRecord* Find(ScenePrefabInstanceHandle handle) const noexcept;
    [[nodiscard]] ScenePrefabInstanceRecord* FindMutable(ScenePrefabInstanceHandle handle) noexcept;
    [[nodiscard]] ScenePrefabInstanceHandle FindRootInstance(SceneObject object) const noexcept;
    [[nodiscard]] ScenePrefabInstanceHandle FindContainingInstance(SceneObject object, std::uint32_t& nodeIndex) const noexcept;
    [[nodiscard]] ScenePrefabInstanceHandle FindContainingInstance(SceneObject object, std::uint32_t& nodeIndex, std::uint64_t& nodeId) const noexcept;
    [[nodiscard]] ScenePrefabInstanceHandle FindContainingEntity(SceneEntity entity, std::uint32_t& nodeIndex) const noexcept;
    [[nodiscard]] std::vector<ScenePrefabInstanceHandle> Handles() const;
    [[nodiscard]] std::vector<ScenePrefabInstanceHandle> HandlesForPrefab(ScenePrefabHandle prefab) const;
    [[nodiscard]] bool ContainsExactlyPrefabHandles(ScenePrefabHandle prefab, std::span<const ScenePrefabInstanceHandle> sortedHandles) const noexcept;
    void MarkNodeDirty(ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex);
    void MarkTopologyDirty(ScenePrefabInstanceHandle handle);
    [[nodiscard]] std::span<const std::uint32_t> DirtyNodes(ScenePrefabInstanceHandle handle) const noexcept;
    [[nodiscard]] bool TopologyDirty(ScenePrefabInstanceHandle handle) const noexcept;
    void ClearDirtyNodes(ScenePrefabInstanceHandle handle) noexcept;
    [[nodiscard]] std::size_t Count() const noexcept;
    void ReindexObjects(ScenePrefabInstanceHandle handle, std::span<const SceneObject> oldObjects) noexcept;
    [[nodiscard]] bool UpdateSource(ScenePrefabInstanceHandle handle, ScenePrefabHandle prefab, std::string prefabGuid);
    [[nodiscard]] bool Remove(ScenePrefabInstanceHandle handle) noexcept;
    void Clear() noexcept;

private:
    struct ObjectIndexEntry {
        ScenePrefabInstanceHandle instance;
        std::uint32_t nodeIndex = 0;
    };

    void IndexRecord(ScenePrefabInstanceHandle handle, const ScenePrefabInstanceRecord& record);
    void IndexObjects(ScenePrefabInstanceHandle handle, const ScenePrefabInstanceRecord& record);
    void IndexContiguousDenseObjectRange(ScenePrefabInstanceHandle handle, std::uint32_t firstDenseIndex, std::size_t objectCount) noexcept;
    void IndexContiguousDensePreparedObjects(ScenePrefabInstanceHandle handle, const ScenePrefabInstanceRecord& record) noexcept;
    void IndexCreatedDenseObjectSpanTrusted(ScenePrefabInstanceHandle handle, std::span<const SceneObject> objects) noexcept;
    void IndexDensePreparedObjectSpan(ScenePrefabInstanceHandle handle, std::span<const SceneObject> objects) noexcept;
    void IndexDensePreparedObjects(ScenePrefabInstanceHandle handle, const ScenePrefabInstanceRecord& record) noexcept;
    void UnindexObjects(ScenePrefabInstanceHandle handle, std::span<const SceneObject> objects) noexcept;
    void RemoveFromPrefabIndex(ScenePrefabHandle prefab, ScenePrefabInstanceHandle handle) noexcept;
    void EnsureRecordSlot(ScenePrefabInstanceHandle handle);
    void EnsureRecordSlots(std::uint64_t firstId, std::size_t count);
    [[nodiscard]] std::uint64_t NodeIdFor(ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex) const noexcept;
    [[nodiscard]] bool RecordSlotAlive(ScenePrefabInstanceHandle handle) const noexcept;
    [[nodiscard]] static std::size_t RecordSlotIndex(ScenePrefabInstanceHandle handle) noexcept;

    std::uint64_t nextId_ = 1;
    std::vector<ScenePrefabInstanceRecord> records_;
    std::vector<std::uint8_t> recordAlive_;
    std::size_t liveRecordCount_ = 0;
    std::map<ScenePrefabHandle, std::vector<ScenePrefabInstanceHandle>> prefabIndex_;
    std::vector<ScenePrefabInstanceHandle> denseRootIndex_;
    std::vector<ObjectIndexEntry> denseObjectIndex_;
    std::unordered_map<SceneEntity::IdType, ScenePrefabInstanceHandle> rootIndex_;
    std::unordered_map<SceneEntity::IdType, ObjectIndexEntry> objectIndex_;
    std::vector<std::shared_ptr<const std::string>> batchPrefabGuidPool_;
    std::vector<std::shared_ptr<const std::vector<std::uint64_t>>> batchNodeIdPool_;
    std::vector<std::shared_ptr<const ScenePrefab>> batchResolvedPrefabPool_;
    std::vector<std::shared_ptr<const std::vector<SceneObject>>> batchObjectSlabPool_;
};

} // namespace kb::scene
