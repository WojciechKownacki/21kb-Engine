#pragma once

#include "engine/ecs/ChunkSizeProfile.hpp"
#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/ComponentStoragePolicy.hpp"
#include "engine/ecs/Entity.hpp"
#include "engine/ecs/QueryExecutionScratch.hpp"
#include "engine/ecs/WorldConfig.hpp"
#include "engine/ecs/WorldSnapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace kb::ecs {

struct NativeComponentType {
    ComponentId id = 0;
    std::size_t size = 0;
    std::size_t alignment = 1;
    ComponentStorageClass storageClass = ComponentStorageClass::HotTable;
};

struct NativeComponentValue {
    NativeComponentType type{};
    const void* data = nullptr;
};

struct NativeBulkComponentColumn {
    NativeComponentType type{};
    const void* data = nullptr;
    std::size_t stride = 0;
    std::size_t sourceCount = 0;
};

struct NativeArchetypeCapacityReport {
    std::size_t chunkPayloadBytes = 0;
    std::size_t entitiesPerChunk = 0;
    std::size_t bytesPerEntity = 0;
    std::size_t hotBytesPerEntity = 0;
    std::size_t nonHotBytesPerEntity = 0;
    std::size_t hotOnlyEntitiesPerChunk = 0;
    std::size_t capacityLostToNonHotStorage = 0;
    std::size_t usedPayloadBytes = 0;
    std::size_t hotOnlyUsedPayloadBytes = 0;
    std::size_t nonHotUsedPayloadBytes = 0;
    std::size_t sidePayloadBytes = 0;
    std::size_t wastedPayloadBytes = 0;

    [[nodiscard]] constexpr bool FitsAtLeastOneEntity() const noexcept {
        return entitiesPerChunk > 0U;
    }
};

struct NativeEcsChunkMemoryCounters {
    std::size_t archetypeIndex = 0;
    std::size_t chunkIndex = 0;
    std::size_t liveEntities = 0;
    std::size_t capacity = 0;
    std::size_t hotOnlyCapacity = 0;
    std::size_t capacityLostToNonHotStorage = 0;
    std::size_t payloadBytes = 0;
    std::size_t sidePayloadBytes = 0;
    std::size_t usedBytes = 0;
    std::size_t wastedBytes = 0;
    std::size_t metadataBytes = 0;
};

struct NativeEcsArchetypeMemoryCounters {
    std::size_t archetypeIndex = 0;
    std::vector<ComponentId> componentIds;
    std::size_t liveEntities = 0;
    std::size_t chunks = 0;
    std::size_t capacity = 0;
    std::size_t hotOnlyCapacity = 0;
    std::size_t capacityLostToNonHotStorage = 0;
    std::size_t payloadBytes = 0;
    std::size_t sidePayloadBytes = 0;
    std::size_t usedBytes = 0;
    std::size_t wastedBytes = 0;
    std::size_t metadataBytes = 0;
    std::size_t hotTableComponents = 0;
    std::size_t coldTableComponents = 0;
    std::size_t sparseTagComponents = 0;
    std::size_t sparsePayloadComponents = 0;
    std::size_t sharedValueComponents = 0;
    std::size_t externalBlobComponents = 0;
    std::size_t hotTableUsedBytes = 0;
    std::size_t coldTableUsedBytes = 0;
    std::size_t sparseTagUsedBytes = 0;
    std::size_t sparsePayloadUsedBytes = 0;
    std::size_t sharedValueUsedBytes = 0;
    std::size_t externalBlobUsedBytes = 0;
    std::size_t hotTableCapacityBytes = 0;
    std::size_t coldTableCapacityBytes = 0;
    std::size_t sparseTagCapacityBytes = 0;
    std::size_t sparsePayloadCapacityBytes = 0;
    std::size_t sharedValueCapacityBytes = 0;
    std::size_t externalBlobCapacityBytes = 0;
    std::uint64_t version = 0;
    std::vector<NativeEcsChunkMemoryCounters> chunkCounters;
};

struct NativeEcsStorageStats {
    std::size_t chunks = 0;
    std::size_t capacity = 0;
    std::size_t hotOnlyCapacity = 0;
    std::size_t capacityLostToNonHotStorage = 0;
    std::size_t sparseChunks = 0;
    std::size_t tailSparseChunks = 0;
    std::size_t fragmentedChunks = 0;
    std::size_t emptyChunks = 0;
    std::size_t chunkPoolAllocated = 0;
    std::size_t chunkPoolInUse = 0;
    std::size_t chunkPoolFree = 0;
    std::size_t chunkPoolAcquireCount = 0;
    std::size_t chunkPoolReuseCount = 0;
    std::size_t chunkPoolReleaseCount = 0;
    std::size_t chunkPoolTrimCount = 0;
    std::size_t chunkPoolSystemAllocationCount = 0;
    std::size_t chunkPoolPeakAllocated = 0;
    std::size_t activePayloadBytes = 0;
    std::size_t activeSidePayloadBytes = 0;
    std::size_t committedPayloadBytes = 0;
    std::size_t freePayloadBytes = 0;
    std::size_t peakCommittedPayloadBytes = 0;
    std::size_t chunkMetadataBytes = 0;
    std::size_t entityRecordBytes = 0;
    std::size_t trackedBytes = 0;
    std::size_t usedBytes = 0;
    std::size_t wastedBytes = 0;
    std::size_t archetypeCount = 0;
    std::size_t liveEntities = 0;
    std::size_t hotTableComponents = 0;
    std::size_t coldTableComponents = 0;
    std::size_t sparseTagComponents = 0;
    std::size_t sparsePayloadComponents = 0;
    std::size_t sharedValueComponents = 0;
    std::size_t externalBlobComponents = 0;
    std::size_t hotTableUsedBytes = 0;
    std::size_t coldTableUsedBytes = 0;
    std::size_t sparseTagUsedBytes = 0;
    std::size_t sparsePayloadUsedBytes = 0;
    std::size_t sharedValueUsedBytes = 0;
    std::size_t externalBlobUsedBytes = 0;
    std::size_t hotTableCapacityBytes = 0;
    std::size_t coldTableCapacityBytes = 0;
    std::size_t sparseTagCapacityBytes = 0;
    std::size_t sparsePayloadCapacityBytes = 0;
    std::size_t sharedValueCapacityBytes = 0;
    std::size_t externalBlobCapacityBytes = 0;
    std::vector<NativeEcsArchetypeMemoryCounters> archetypeCounters;
};

struct NativeComponentDirtyRange {
    std::size_t begin = 0;
    std::size_t count = 0;
    std::size_t dirtyCount = 0;
};

struct NativeEcsMaintenanceBudget {
    std::size_t maxFreeChunksToKeep = std::numeric_limits<std::size_t>::max();
    std::size_t maxChunksToRelease = std::numeric_limits<std::size_t>::max();
};

struct NativeEcsMaintenanceStats {
    std::size_t freeChunksBefore = 0;
    std::size_t freeChunksAfter = 0;
    std::size_t chunkPoolAllocatedBefore = 0;
    std::size_t chunkPoolAllocatedAfter = 0;
    std::size_t chunksReleasedToSystem = 0;
    std::size_t fragmentedChunksBefore = 0;
    std::size_t fragmentedChunksAfter = 0;
    std::size_t emptyChunksBefore = 0;
    std::size_t emptyChunksAfter = 0;
    bool budgetExhausted = false;
};

struct NativeArchetypeMatch {
    std::size_t archetypeIndex = 0;
    std::size_t liveEntities = 0;
    std::uint64_t version = 0;
};

class NativeArchetypeStorage {
public:
    explicit NativeArchetypeStorage(WorldConfig config = WorldConfig{});
    ~NativeArchetypeStorage();

    NativeArchetypeStorage(const NativeArchetypeStorage&) = delete;
    NativeArchetypeStorage& operator=(const NativeArchetypeStorage&) = delete;
    NativeArchetypeStorage(NativeArchetypeStorage&&) noexcept;
    NativeArchetypeStorage& operator=(NativeArchetypeStorage&&) noexcept;

    [[nodiscard]] Entity CreateEntity(std::span<const NativeComponentValue> components = {});
    [[nodiscard]] std::vector<Entity> CreateEntities(std::size_t count, std::span<const NativeBulkComponentColumn> components = {});
    void CreateEntitiesInto(std::vector<Entity>& output, std::size_t count, std::span<const NativeBulkComponentColumn> components = {});
    void AdoptEntity(Entity entity, std::span<const NativeComponentValue> components = {});
    void AdoptEntities(std::span<const Entity> entities, std::span<const NativeBulkComponentColumn> components = {});
    void DestroyEntity(Entity entity);
    void DestroyEntities(std::span<const Entity> entities);
    void Clear();
    void ClearRetainingCapacity();
    [[nodiscard]] bool IsAlive(Entity entity) const noexcept;
    [[nodiscard]] Entity ResolveAliveEntity(Entity::IdType entityIdWithoutGeneration) const noexcept;

    void AddComponents(Entity entity, std::span<const NativeComponentValue> components);
    void AddComponents(std::span<const Entity> entities, std::span<const NativeBulkComponentColumn> components);
    void RemoveComponents(Entity entity, std::span<const ComponentId> componentIds);
    void RemoveComponents(std::span<const Entity> entities, std::span<const ComponentId> componentIds);

    void SetComponent(Entity entity, ComponentId componentId, const void* data, std::size_t size);
    void SetComponents(std::span<const Entity> entities, std::span<const NativeBulkComponentColumn> components);
    void MarkComponentModified(Entity entity, ComponentId componentId);
    void MarkArchetypeComponentsModified(std::size_t archetypeIndex, std::span<const ComponentId> componentIds);
    void MarkArchetypeChunkComponentsModified(
        std::size_t archetypeIndex,
        std::size_t chunkIndex,
        std::size_t firstRow,
        std::size_t count,
        std::span<const ComponentId> componentIds);
    [[nodiscard]] std::size_t ComponentDirtyCount(std::size_t archetypeIndex, std::size_t chunkIndex, ComponentId componentId) const;
    [[nodiscard]] std::size_t CollectComponentDirtyRanges(
        std::size_t archetypeIndex,
        std::size_t chunkIndex,
        ComponentId componentId,
        std::size_t rangeSize,
        std::vector<NativeComponentDirtyRange>& ranges) const;
    void ClearComponentDirtyRows(
        std::size_t archetypeIndex,
        std::size_t chunkIndex,
        ComponentId componentId,
        std::size_t firstRow = 0,
        std::size_t count = std::numeric_limits<std::size_t>::max());
    [[nodiscard]] void* MutableComponentData(Entity entity, ComponentId componentId);
    [[nodiscard]] const void* ComponentData(Entity entity, ComponentId componentId) const;
    [[nodiscard]] bool HasComponent(Entity entity, ComponentId componentId) const;
    [[nodiscard]] bool EntityArchetypeMatches(Entity entity, std::span<const ComponentId> requiredComponentIds) const;
    [[nodiscard]] bool EntityArchetypeMatches(Entity entity, std::span<const ComponentId> requiredComponentIds, std::span<const ComponentId> excludedComponentIds) const;
    [[nodiscard]] std::vector<NativeArchetypeMatch> MatchingArchetypes(std::span<const ComponentId> requiredComponentIds) const;
    void CollectQueryRecords(
        std::span<const ComponentId> componentIds,
        std::span<const ComponentId> requiredComponentIds,
        std::span<const ComponentId> excludedComponentIds,
        std::vector<QueryTableDispatchRecord>& records) const;
    void CollectMutableQueryRecords(
        std::span<const ComponentId> componentIds,
        std::span<const ComponentId> requiredComponentIds,
        std::span<const ComponentId> excludedComponentIds,
        std::vector<MutableQueryTableDispatchRecord>& records);
    void CaptureChunkedSnapshot(std::span<const ComponentTypeInfo> componentTypes, ChunkedWorldSnapshot& snapshot) const;
    void CaptureChunkedDeltaSnapshot(
        std::span<const ComponentTypeInfo> componentTypes,
        const ChunkedWorldSnapshot& baseline,
        ChunkedWorldDeltaSnapshot& delta) const;
    [[nodiscard]] bool StreamChunkedSnapshot(
        std::span<const ComponentTypeInfo> componentTypes,
        ChunkedWorldSnapshotChunkVisitor visitor,
        void* context) const;

    [[nodiscard]] std::uint64_t ArchetypeVersion(Entity entity) const;
    [[nodiscard]] std::uint64_t ComponentVersion(Entity entity, ComponentId componentId) const;
    [[nodiscard]] std::uint64_t ArchetypeComponentVersion(std::size_t archetypeIndex, ComponentId componentId) const;
    [[nodiscard]] std::uint64_t StructuralVersion() const noexcept;
    [[nodiscard]] std::size_t ChunkCount() const noexcept;
    [[nodiscard]] std::size_t ChunkPayloadBytes() const noexcept;
    [[nodiscard]] NativeEcsStorageStats Stats() const;
    [[nodiscard]] NativeEcsMaintenanceStats MaintainChunks(NativeEcsMaintenanceBudget budget);

private:
    class Impl;

    Impl* impl_ = nullptr;
};

[[nodiscard]] NativeArchetypeCapacityReport EstimateNativeArchetypeCapacity(
    std::span<const NativeComponentType> componentTypes,
    std::size_t chunkPayloadBytes);

[[nodiscard]] NativeArchetypeCapacityReport EstimateNativeArchetypeCapacity(
    std::span<const NativeComponentType> componentTypes,
    ChunkSizeProfile profile);

} // namespace kb::ecs
