#pragma once

#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabInstanceHandle.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefab.hpp"

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
    std::vector<std::uint64_t> nodeIds;
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
        if (sharedObjects != nullptr) {
            return std::span<const SceneObject>{ *sharedObjects };
        }
        return std::span<const SceneObject>{ objects };
    }

    [[nodiscard]] std::vector<SceneObject>& MutableObjects() {
        if (sharedObjects != nullptr) {
            objects.assign(sharedObjects->begin(), sharedObjects->end());
            sharedObjects.reset();
        }
        return objects;
    }

    void SetObjects(std::vector<SceneObject> updatedObjects) {
        sharedObjects.reset();
        objects = std::move(updatedObjects);
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
    [[nodiscard]] bool Contains(ScenePrefabInstanceHandle handle) const noexcept;
    [[nodiscard]] const ScenePrefabInstanceRecord* Find(ScenePrefabInstanceHandle handle) const noexcept;
    [[nodiscard]] ScenePrefabInstanceRecord* FindMutable(ScenePrefabInstanceHandle handle) noexcept;
    [[nodiscard]] ScenePrefabInstanceHandle FindRootInstance(SceneObject object) const noexcept;
    [[nodiscard]] ScenePrefabInstanceHandle FindContainingInstance(SceneObject object, std::uint32_t& nodeIndex) const noexcept;
    [[nodiscard]] ScenePrefabInstanceHandle FindContainingInstance(SceneObject object, std::uint32_t& nodeIndex, std::uint64_t& nodeId) const noexcept;
    [[nodiscard]] std::vector<ScenePrefabInstanceHandle> Handles() const;
    [[nodiscard]] std::vector<ScenePrefabInstanceHandle> HandlesForPrefab(ScenePrefabHandle prefab) const;
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
    void IndexContiguousDensePreparedObjects(ScenePrefabInstanceHandle handle, const ScenePrefabInstanceRecord& record) noexcept;
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
};

} // namespace kb::scene
