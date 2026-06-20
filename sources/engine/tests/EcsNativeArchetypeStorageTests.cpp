#include "EcsTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/CommandBuffer.hpp"
#include "engine/ecs/HotArchetypeLayoutAdvisor.hpp"
#include "engine/ecs/NativeArchetypeStorage.hpp"
#include "engine/ecs/World.hpp"

#include <flecs.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

struct Position {
    float x = 0.0F;
    float y = 0.0F;
};

struct Velocity {
    float x = 0.0F;
    float y = 0.0F;
};

struct Mass {
    float value = 0.0F;
};

struct RuntimeTag {
    std::uint32_t value = 0;
};

struct alignas(32) WideAlignedComponent {
    std::array<float, 8U> values{};
};

constexpr kb::ecs::ComponentId kPositionId = 101;
constexpr kb::ecs::ComponentId kVelocityId = 102;
constexpr kb::ecs::ComponentId kMassId = 103;
constexpr kb::ecs::ComponentId kRuntimeTagId = 104;
constexpr kb::ecs::ComponentId kWideAlignedId = 105;

template <typename T>
[[nodiscard]] constexpr kb::ecs::NativeComponentType ComponentType(kb::ecs::ComponentId id) noexcept {
    return kb::ecs::NativeComponentType{ .id = id, .size = sizeof(T), .alignment = alignof(T) };
}

template <typename T>
[[nodiscard]] const T& Component(const kb::ecs::NativeArchetypeStorage& storage, kb::ecs::Entity entity, kb::ecs::ComponentId id) {
    return *static_cast<const T*>(storage.ComponentData(entity, id));
}

template <typename T>
[[nodiscard]] const T& BackendComponent(const kb::ecs::World& world, kb::ecs::Entity entity, kb::ecs::ComponentId id) {
    const void* component = ecs_get_id(world.NativeHandle(), ecs_strip_generation(entity.Id()), id);
    kb::tests::Require(component != nullptr, "World mirrored backend component is missing");
    return *static_cast<const T*>(component);
}

[[nodiscard]] bool IsAligned(const void* pointer, std::size_t alignment) noexcept {
    return pointer != nullptr && (reinterpret_cast<std::uintptr_t>(pointer) % alignment) == 0U;
}

void RequireStorageStatsConsistent(const kb::ecs::NativeEcsStorageStats& stats, std::size_t expectedLiveEntities, const char* message) {
    std::size_t countedChunks = 0;
    std::size_t countedLiveEntities = 0;
    std::size_t countedCapacity = 0;
    std::size_t countedHotOnlyCapacity = 0;
    std::size_t countedCapacityLostToNonHotStorage = 0;
    std::size_t countedSidePayloadBytes = 0;
    std::size_t countedSparseChunks = 0;
    std::size_t countedTailSparseChunks = 0;
    std::size_t countedFragmentedChunks = 0;
    std::size_t countedEmptyChunks = 0;
    std::size_t countedUsedBytes = 0;
    std::size_t countedWastedBytes = 0;
    std::size_t countedHotTableComponents = 0;
    std::size_t countedColdTableComponents = 0;
    std::size_t countedSparseTagComponents = 0;
    std::size_t countedSparsePayloadComponents = 0;
    std::size_t countedSharedValueComponents = 0;
    std::size_t countedExternalBlobComponents = 0;
    std::size_t countedHotTableUsedBytes = 0;
    std::size_t countedColdTableUsedBytes = 0;
    std::size_t countedSparseTagUsedBytes = 0;
    std::size_t countedSparsePayloadUsedBytes = 0;
    std::size_t countedSharedValueUsedBytes = 0;
    std::size_t countedExternalBlobUsedBytes = 0;
    std::size_t countedHotTableCapacityBytes = 0;
    std::size_t countedColdTableCapacityBytes = 0;
    std::size_t countedSparseTagCapacityBytes = 0;
    std::size_t countedSparsePayloadCapacityBytes = 0;
    std::size_t countedSharedValueCapacityBytes = 0;
    std::size_t countedExternalBlobCapacityBytes = 0;

    for (const kb::ecs::NativeEcsArchetypeMemoryCounters& archetype : stats.archetypeCounters) {
        countedHotTableComponents += archetype.hotTableComponents;
        countedColdTableComponents += archetype.coldTableComponents;
        countedSparseTagComponents += archetype.sparseTagComponents;
        countedSparsePayloadComponents += archetype.sparsePayloadComponents;
        countedSharedValueComponents += archetype.sharedValueComponents;
        countedExternalBlobComponents += archetype.externalBlobComponents;
        countedHotTableUsedBytes += archetype.hotTableUsedBytes;
        countedColdTableUsedBytes += archetype.coldTableUsedBytes;
        countedSparseTagUsedBytes += archetype.sparseTagUsedBytes;
        countedSparsePayloadUsedBytes += archetype.sparsePayloadUsedBytes;
        countedSharedValueUsedBytes += archetype.sharedValueUsedBytes;
        countedExternalBlobUsedBytes += archetype.externalBlobUsedBytes;
        countedHotTableCapacityBytes += archetype.hotTableCapacityBytes;
        countedColdTableCapacityBytes += archetype.coldTableCapacityBytes;
        countedSparseTagCapacityBytes += archetype.sparseTagCapacityBytes;
        countedSparsePayloadCapacityBytes += archetype.sparsePayloadCapacityBytes;
        countedSharedValueCapacityBytes += archetype.sharedValueCapacityBytes;
        countedExternalBlobCapacityBytes += archetype.externalBlobCapacityBytes;
        std::size_t archetypeRows = 0;
        std::size_t archetypeUsedBytes = 0;
        std::size_t archetypeWastedBytes = 0;
        std::size_t archetypePayloadBytes = 0;
        std::size_t archetypeSidePayloadBytes = 0;
        std::size_t archetypeMetadataBytes = 0;
        for (const kb::ecs::NativeEcsChunkMemoryCounters& chunk : archetype.chunkCounters) {
            archetypeRows += chunk.liveEntities;
            if (chunk.liveEntities == 0U) {
                ++countedEmptyChunks;
            } else if (chunk.liveEntities < chunk.capacity) {
                ++countedSparseChunks;
                if (chunk.chunkIndex + 1U == archetype.chunkCounters.size()) {
                    ++countedTailSparseChunks;
                } else {
                    ++countedFragmentedChunks;
                }
            }
            archetypeUsedBytes += chunk.usedBytes;
            archetypeWastedBytes += chunk.wastedBytes;
            archetypePayloadBytes += chunk.payloadBytes;
            archetypeSidePayloadBytes += chunk.sidePayloadBytes;
            archetypeMetadataBytes += chunk.metadataBytes;
            kb::tests::Require(chunk.usedBytes + chunk.wastedBytes == chunk.payloadBytes, message);
            kb::tests::Require(chunk.metadataBytes >= chunk.capacity * sizeof(kb::ecs::Entity), message);
        }
        kb::tests::Require(archetypeRows == archetype.liveEntities, message);
        kb::tests::Require(archetypeUsedBytes == archetype.usedBytes, message);
        kb::tests::Require(archetypeWastedBytes == archetype.wastedBytes, message);
        kb::tests::Require(archetypePayloadBytes == archetype.payloadBytes, message);
        kb::tests::Require(archetypeSidePayloadBytes == archetype.sidePayloadBytes, message);
        kb::tests::Require(archetypeMetadataBytes == archetype.metadataBytes, message);

        countedChunks += archetype.chunks;
        countedLiveEntities += archetype.liveEntities;
        countedCapacity += archetype.capacity;
        countedHotOnlyCapacity += archetype.hotOnlyCapacity;
        countedCapacityLostToNonHotStorage += archetype.capacityLostToNonHotStorage;
        countedSidePayloadBytes += archetype.sidePayloadBytes;
        countedUsedBytes += archetype.usedBytes;
        countedWastedBytes += archetype.wastedBytes;
    }

    kb::tests::Require(stats.archetypeCounters.size() == stats.archetypeCount, message);
    kb::tests::Require(stats.liveEntities == expectedLiveEntities, message);
    kb::tests::Require(countedLiveEntities == expectedLiveEntities, message);
    kb::tests::Require(stats.chunks == countedChunks, message);
    kb::tests::Require(stats.capacity == countedCapacity, message);
    kb::tests::Require(stats.hotOnlyCapacity == countedHotOnlyCapacity, message);
    kb::tests::Require(stats.capacityLostToNonHotStorage == countedCapacityLostToNonHotStorage, message);
    kb::tests::Require(stats.sparseChunks == countedSparseChunks, message);
    kb::tests::Require(stats.tailSparseChunks == countedTailSparseChunks, message);
    kb::tests::Require(stats.fragmentedChunks == countedFragmentedChunks, message);
    kb::tests::Require(stats.emptyChunks == countedEmptyChunks, message);
    kb::tests::Require(stats.fragmentedChunks == 0U, message);
    kb::tests::Require(stats.emptyChunks == 0U, message);
    kb::tests::Require(stats.chunkPoolInUse == stats.chunks, message);
    kb::tests::Require(stats.chunkPoolAllocated == stats.chunkPoolInUse + stats.chunkPoolFree, message);
    kb::tests::Require(stats.chunkPoolAcquireCount >= stats.chunkPoolInUse, message);
    kb::tests::Require(stats.chunkPoolReleaseCount >= stats.chunkPoolFree, message);
    kb::tests::Require(stats.chunkPoolSystemAllocationCount >= stats.chunkPoolAllocated, message);
    kb::tests::Require(stats.chunkPoolPeakAllocated >= stats.chunkPoolAllocated, message);
    kb::tests::Require(stats.activePayloadBytes <= stats.committedPayloadBytes, message);
    kb::tests::Require(stats.activePayloadBytes + stats.activeSidePayloadBytes >= stats.usedBytes, message);
    kb::tests::Require(stats.committedPayloadBytes == stats.activePayloadBytes + stats.freePayloadBytes, message);
    kb::tests::Require(stats.peakCommittedPayloadBytes >= stats.committedPayloadBytes, message);
    kb::tests::Require(stats.activeSidePayloadBytes == countedSidePayloadBytes, message);
    kb::tests::Require(stats.trackedBytes == stats.committedPayloadBytes + stats.activeSidePayloadBytes + stats.chunkMetadataBytes + stats.entityRecordBytes, message);
    kb::tests::Require(stats.trackedBytes >= stats.committedPayloadBytes, message);
    kb::tests::Require(stats.usedBytes == countedUsedBytes, message);
    kb::tests::Require(stats.wastedBytes == countedWastedBytes, message);
    kb::tests::Require(stats.hotTableComponents == countedHotTableComponents, message);
    kb::tests::Require(stats.coldTableComponents == countedColdTableComponents, message);
    kb::tests::Require(stats.sparseTagComponents == countedSparseTagComponents, message);
    kb::tests::Require(stats.sparsePayloadComponents == countedSparsePayloadComponents, message);
    kb::tests::Require(stats.sharedValueComponents == countedSharedValueComponents, message);
    kb::tests::Require(stats.externalBlobComponents == countedExternalBlobComponents, message);
    kb::tests::Require(stats.hotTableUsedBytes == countedHotTableUsedBytes, message);
    kb::tests::Require(stats.coldTableUsedBytes == countedColdTableUsedBytes, message);
    kb::tests::Require(stats.sparseTagUsedBytes == countedSparseTagUsedBytes, message);
    kb::tests::Require(stats.sparsePayloadUsedBytes == countedSparsePayloadUsedBytes, message);
    kb::tests::Require(stats.sharedValueUsedBytes == countedSharedValueUsedBytes, message);
    kb::tests::Require(stats.externalBlobUsedBytes == countedExternalBlobUsedBytes, message);
    kb::tests::Require(stats.hotTableCapacityBytes == countedHotTableCapacityBytes, message);
    kb::tests::Require(stats.coldTableCapacityBytes == countedColdTableCapacityBytes, message);
    kb::tests::Require(stats.sparseTagCapacityBytes == countedSparseTagCapacityBytes, message);
    kb::tests::Require(stats.sparsePayloadCapacityBytes == countedSparsePayloadCapacityBytes, message);
    kb::tests::Require(stats.sharedValueCapacityBytes == countedSharedValueCapacityBytes, message);
    kb::tests::Require(stats.externalBlobCapacityBytes == countedExternalBlobCapacityBytes, message);
}

void RunChunkProfileAndStatsTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk16KB;
    config.reserveEntities = 512;
    config.reserveArchetypes = 4;

    kb::ecs::NativeArchetypeStorage storage{ config };
    kb::tests::Require(
        storage.ChunkPayloadBytes() == kb::ecs::ChunkPayloadBytes(kb::ecs::ChunkSizeProfile::Chunk16KB),
        "native ECS storage did not bind ChunkSizeProfile to chunk payload bytes");

    Position position{ .x = 3.0F, .y = 4.0F };
    const std::array components{
        kb::ecs::NativeComponentValue{ .type = ComponentType<Position>(kPositionId), .data = &position },
    };
    const kb::ecs::Entity entity = storage.CreateEntity(components);

    const kb::ecs::NativeEcsStorageStats stats = storage.Stats();
    kb::tests::Require(storage.IsAlive(entity), "native ECS storage did not create a live generational entity");
    kb::tests::Require(stats.liveEntities == 1, "native ECS storage stats did not count live entities");
    kb::tests::Require(stats.archetypeCount == 1, "native ECS storage stats did not count archetypes");
    kb::tests::Require(stats.chunks == 1, "native ECS storage did not allocate a chunk");
    kb::tests::Require(storage.ChunkCount() == stats.chunks, "native ECS lightweight chunk count disagreed with storage stats");
    kb::tests::Require(stats.usedBytes >= sizeof(Position), "native ECS storage stats did not count used component bytes");
    kb::tests::Require(stats.wastedBytes < storage.ChunkPayloadBytes(), "native ECS storage reported invalid wasted bytes");

    storage.DestroyEntity(entity);
    const kb::ecs::NativeEcsStorageStats emptyStats = storage.Stats();
    kb::tests::Require(!storage.IsAlive(entity), "native ECS storage did not invalidate a destroyed entity");
    kb::tests::Require(emptyStats.liveEntities == 0, "native ECS storage stats did not drop destroyed entities");
    kb::tests::Require(emptyStats.chunks == 0, "native ECS storage did not return an empty chunk to the free list");
    kb::tests::Require(storage.ChunkCount() == emptyStats.chunks, "native ECS lightweight chunk count did not track empty storage");
}

void RunArchetypeCapacityReportTest() {
    const std::array componentTypes{
        ComponentType<Position>(kPositionId),
        ComponentType<Velocity>(kVelocityId),
        ComponentType<Mass>(kMassId),
    };

    const kb::ecs::NativeArchetypeCapacityReport report = kb::ecs::EstimateNativeArchetypeCapacity(
        componentTypes,
        kb::ecs::ChunkSizeProfile::Chunk4KB);
    kb::tests::Require(report.chunkPayloadBytes == kb::ecs::ChunkPayloadBytes(kb::ecs::ChunkSizeProfile::Chunk4KB), "native ECS capacity report used an invalid chunk payload");
    kb::tests::Require(report.entitiesPerChunk > 0U, "native ECS capacity report did not fit a compact archetype");
    kb::tests::Require(report.bytesPerEntity == sizeof(Position) + sizeof(Velocity) + sizeof(Mass), "native ECS capacity report produced invalid bytes per entity");
    kb::tests::Require(report.hotBytesPerEntity == report.bytesPerEntity, "native ECS capacity report misclassified default hot bytes");
    kb::tests::Require(report.nonHotBytesPerEntity == 0U, "native ECS capacity report reported non-hot bytes for default hot components");
    kb::tests::Require(report.hotOnlyEntitiesPerChunk == report.entitiesPerChunk, "native ECS capacity report lost capacity for an all-hot archetype");
    kb::tests::Require(report.capacityLostToNonHotStorage == 0U, "native ECS capacity report reported non-hot capacity loss for an all-hot archetype");
    kb::tests::Require(report.hotOnlyUsedPayloadBytes == report.usedPayloadBytes, "native ECS capacity report diverged hot-only and full payload for an all-hot archetype");
    kb::tests::Require(report.nonHotUsedPayloadBytes == 0U, "native ECS capacity report reported non-hot payload for an all-hot archetype");
    kb::tests::Require(report.sidePayloadBytes == 0U, "native ECS capacity report reported side payload for an all-hot archetype");
    kb::tests::Require(report.usedPayloadBytes + report.wastedPayloadBytes == report.chunkPayloadBytes, "native ECS capacity report did not account for the whole payload");

    const kb::ecs::NativeArchetypeCapacityReport larger = kb::ecs::EstimateNativeArchetypeCapacity(
        componentTypes,
        kb::ecs::ChunkSizeProfile::Chunk16KB);
    kb::tests::Require(larger.entitiesPerChunk > report.entitiesPerChunk, "native ECS capacity report did not scale with chunk size");

    const std::array wideTypes{
        ComponentType<WideAlignedComponent>(kWideAlignedId),
    };
    const kb::ecs::NativeArchetypeCapacityReport wide = kb::ecs::EstimateNativeArchetypeCapacity(
        wideTypes,
        kb::ecs::ChunkSizeProfile::Chunk4KB);
    kb::tests::Require(wide.FitsAtLeastOneEntity(), "native ECS capacity report rejected a valid aligned component");
    kb::tests::Require(wide.usedPayloadBytes % alignof(WideAlignedComponent) == 0U, "native ECS capacity report did not preserve aligned payload layout");

    const std::array mixedTypes{
        ComponentType<Position>(kPositionId),
        kb::ecs::NativeComponentType{
            .id = kVelocityId,
            .size = sizeof(Velocity),
            .alignment = alignof(Velocity),
            .storageClass = kb::ecs::ComponentStorageClass::ColdTable,
        },
        kb::ecs::NativeComponentType{
            .id = kMassId,
            .size = sizeof(Mass),
            .alignment = alignof(Mass),
            .storageClass = kb::ecs::ComponentStorageClass::SharedValue,
        },
    };
    const kb::ecs::NativeArchetypeCapacityReport mixed = kb::ecs::EstimateNativeArchetypeCapacity(
        mixedTypes,
        kb::ecs::ChunkSizeProfile::Chunk4KB);
    kb::tests::Require(mixed.hotBytesPerEntity == sizeof(Position), "native ECS capacity report missed hot bytes in a mixed archetype");
    kb::tests::Require(
        mixed.nonHotBytesPerEntity == sizeof(Velocity) + sizeof(Mass),
        "native ECS capacity report missed non-hot bytes in a mixed archetype");
    kb::tests::Require(
        mixed.hotOnlyEntitiesPerChunk == mixed.entitiesPerChunk,
        "native ECS capacity report did not recover hot-only capacity for a mixed archetype");
    kb::tests::Require(
        mixed.capacityLostToNonHotStorage > 0U,
        "native ECS capacity report did not expose avoided capacity loss from non-hot side storage");
    kb::tests::Require(
        mixed.nonHotUsedPayloadBytes == mixed.nonHotBytesPerEntity * mixed.entitiesPerChunk,
        "native ECS capacity report produced invalid non-hot payload bytes");
    kb::tests::Require(mixed.sidePayloadBytes >= mixed.nonHotUsedPayloadBytes, "native ECS capacity report missed side payload bytes");
}

void RunChunkPoolReuseAccountingTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk4KB;
    config.reserveEntities = 16;
    config.reserveArchetypes = 2;

    kb::ecs::NativeArchetypeStorage storage{ config };
    Position position{ .x = 1.0F, .y = 2.0F };
    const std::array components{
        kb::ecs::NativeComponentValue{ .type = ComponentType<Position>(kPositionId), .data = &position },
    };

    const kb::ecs::Entity first = storage.CreateEntity(components);
    const kb::ecs::NativeEcsStorageStats afterCreate = storage.Stats();
    kb::tests::Require(afterCreate.chunkPoolAllocated == 1U, "native ECS chunk pool did not count allocated chunks");
    kb::tests::Require(afterCreate.chunkPoolInUse == 1U, "native ECS chunk pool did not count in-use chunks");
    kb::tests::Require(storage.ChunkCount() == afterCreate.chunks, "native ECS chunk count did not track chunk pool allocation");
    kb::tests::Require(afterCreate.chunkPoolFree == 0U, "native ECS chunk pool reported a free chunk too early");
    kb::tests::Require(afterCreate.chunkPoolAcquireCount == 1U, "native ECS chunk pool did not count first acquire");
    kb::tests::Require(afterCreate.chunkPoolReuseCount == 0U, "native ECS chunk pool reported reuse before a release");
    kb::tests::Require(afterCreate.chunkPoolSystemAllocationCount == 1U, "native ECS chunk pool did not count first system allocation");
    kb::tests::Require(afterCreate.chunkPoolPeakAllocated == 1U, "native ECS chunk pool did not count peak allocation");
    kb::tests::Require(afterCreate.activePayloadBytes == storage.ChunkPayloadBytes(), "native ECS chunk pool reported invalid active payload bytes");
    kb::tests::Require(afterCreate.committedPayloadBytes == storage.ChunkPayloadBytes(), "native ECS chunk pool reported invalid committed payload bytes");
    kb::tests::Require(afterCreate.freePayloadBytes == 0U, "native ECS chunk pool reported invalid free payload bytes");
    kb::tests::Require(afterCreate.peakCommittedPayloadBytes == storage.ChunkPayloadBytes(), "native ECS chunk pool reported invalid peak committed payload bytes");

    storage.DestroyEntity(first);
    const kb::ecs::NativeEcsStorageStats afterDestroy = storage.Stats();
    kb::tests::Require(afterDestroy.chunkPoolInUse == 0U, "native ECS chunk pool did not release the empty chunk");
    kb::tests::Require(storage.ChunkCount() == afterDestroy.chunks, "native ECS chunk count did not track chunk release");
    kb::tests::Require(afterDestroy.chunkPoolFree == 1U, "native ECS chunk pool did not retain a free chunk");
    kb::tests::Require(afterDestroy.chunkPoolReleaseCount == 1U, "native ECS chunk pool did not count release");
    kb::tests::Require(afterDestroy.chunkPoolSystemAllocationCount == 1U, "native ECS chunk pool changed system allocation count on release");
    kb::tests::Require(afterDestroy.activePayloadBytes == 0U, "native ECS chunk pool reported active bytes for released chunks");
    kb::tests::Require(afterDestroy.committedPayloadBytes == storage.ChunkPayloadBytes(), "native ECS chunk pool lost committed bytes while retaining a free chunk");
    kb::tests::Require(afterDestroy.freePayloadBytes == storage.ChunkPayloadBytes(), "native ECS chunk pool did not report free retained bytes");
    kb::tests::Require(afterDestroy.peakCommittedPayloadBytes == storage.ChunkPayloadBytes(), "native ECS chunk pool lost peak committed bytes");

    [[maybe_unused]] const kb::ecs::Entity second = storage.CreateEntity(components);
    const kb::ecs::NativeEcsStorageStats afterReuse = storage.Stats();
    kb::tests::Require(afterReuse.chunkPoolAllocated == 1U, "native ECS chunk pool allocated instead of reusing a free chunk");
    kb::tests::Require(afterReuse.chunkPoolInUse == 1U, "native ECS chunk pool did not count reused chunk in use");
    kb::tests::Require(storage.ChunkCount() == afterReuse.chunks, "native ECS chunk count did not track chunk reuse");
    kb::tests::Require(afterReuse.chunkPoolFree == 0U, "native ECS chunk pool did not consume the free chunk");
    kb::tests::Require(afterReuse.chunkPoolAcquireCount == 2U, "native ECS chunk pool did not count second acquire");
    kb::tests::Require(afterReuse.chunkPoolReuseCount == 1U, "native ECS chunk pool did not count reuse");
    kb::tests::Require(afterReuse.chunkPoolSystemAllocationCount == 1U, "native ECS chunk pool allocated from system during reuse");
    kb::tests::Require(afterReuse.chunkPoolPeakAllocated == 1U, "native ECS chunk pool changed peak allocation during reuse");
}

void RunChunkPoolMaintenanceBudgetTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk4KB;
    config.reserveEntities = 2048;
    config.reserveArchetypes = 2;

    kb::ecs::NativeArchetypeStorage storage{ config };
    constexpr std::size_t kEntityCount = 1537U;
    std::vector<Position> positions;
    positions.reserve(kEntityCount);
    for (std::size_t index = 0; index < kEntityCount; ++index) {
        positions.push_back(Position{ .x = static_cast<float>(index), .y = static_cast<float>(index + 1U) });
    }
    const std::array components{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Position>(kPositionId),
            .data = positions.data(),
            .stride = sizeof(Position),
            .sourceCount = positions.size(),
        },
    };

    const std::vector<kb::ecs::Entity> entities = storage.CreateEntities(kEntityCount, std::span<const kb::ecs::NativeBulkComponentColumn>{ components });
    kb::tests::Require(storage.Stats().chunks >= 4U, "native ECS maintenance setup did not allocate multiple chunks");
    storage.DestroyEntities(std::span<const kb::ecs::Entity>{ entities });

    const kb::ecs::NativeEcsStorageStats beforeMaintenance = storage.Stats();
    kb::tests::Require(beforeMaintenance.liveEntities == 0U, "native ECS maintenance setup left live entities");
    kb::tests::Require(beforeMaintenance.chunkPoolFree >= 4U, "native ECS maintenance setup did not retain free chunks");

    const kb::ecs::NativeEcsMaintenanceStats first = storage.MaintainChunks(kb::ecs::NativeEcsMaintenanceBudget{
        .maxFreeChunksToKeep = 1U,
        .maxChunksToRelease = 2U,
    });
    kb::tests::Require(first.freeChunksBefore == beforeMaintenance.chunkPoolFree, "native ECS maintenance did not report free chunks before trim");
    kb::tests::Require(first.chunksReleasedToSystem == 2U, "native ECS maintenance ignored release budget");
    kb::tests::Require(first.freeChunksAfter == first.freeChunksBefore - first.chunksReleasedToSystem, "native ECS maintenance reported invalid free chunk delta");
    kb::tests::Require(first.chunkPoolAllocatedAfter == first.chunkPoolAllocatedBefore - first.chunksReleasedToSystem, "native ECS maintenance reported invalid allocated chunk delta");
    kb::tests::Require(first.budgetExhausted, "native ECS maintenance did not report an exhausted partial budget");
    const kb::ecs::NativeEcsStorageStats afterPartialTrim = storage.Stats();
    kb::tests::Require(afterPartialTrim.peakCommittedPayloadBytes >= afterPartialTrim.committedPayloadBytes, "native ECS maintenance lost peak committed byte telemetry");
    kb::tests::Require(afterPartialTrim.chunkPoolPeakAllocated >= afterPartialTrim.chunkPoolAllocated, "native ECS maintenance lost peak allocation telemetry");

    const kb::ecs::NativeEcsMaintenanceStats second = storage.MaintainChunks(kb::ecs::NativeEcsMaintenanceBudget{
        .maxFreeChunksToKeep = 0U,
    });
    kb::tests::Require(second.freeChunksAfter == 0U, "native ECS maintenance did not trim all free chunks");
    kb::tests::Require(!second.budgetExhausted, "native ECS maintenance reported budget exhaustion after full trim");
    kb::tests::Require(storage.Stats().chunkPoolTrimCount == first.chunksReleasedToSystem + second.chunksReleasedToSystem, "native ECS maintenance did not account trimmed chunks");
    const kb::ecs::NativeEcsStorageStats afterFullTrim = storage.Stats();
    kb::tests::Require(afterFullTrim.committedPayloadBytes == 0U, "native ECS maintenance left committed payload bytes after trimming all free chunks");
    kb::tests::Require(afterFullTrim.peakCommittedPayloadBytes >= beforeMaintenance.committedPayloadBytes, "native ECS maintenance did not preserve peak committed byte telemetry");
}

void RunChunkCommitGuardTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk4KB;
    config.maxNativeStorageCommittedPayloadBytes = kb::ecs::ChunkPayloadBytes(kb::ecs::ChunkSizeProfile::Chunk4KB);

    kb::ecs::NativeArchetypeStorage storage{ config };
    constexpr Position kPosition{ .x = 1.0F, .y = 2.0F };
    const std::array positionComponent{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Position>(kPositionId),
            .data = &kPosition,
            .stride = sizeof(Position),
            .sourceCount = 1U,
        },
    };
    const std::array positionType{ ComponentType<Position>(kPositionId) };
    const kb::ecs::NativeArchetypeCapacityReport capacity = kb::ecs::EstimateNativeArchetypeCapacity(
        positionType,
        kb::ecs::ChunkSizeProfile::Chunk4KB);
    std::vector<kb::ecs::Entity> entities = storage.CreateEntities(capacity.entitiesPerChunk, positionComponent);
    kb::tests::Require(entities.size() == capacity.entitiesPerChunk, "native ECS chunk commit guard blocked the first chunk");
    kb::tests::Require(storage.Stats().committedPayloadBytes == config.maxNativeStorageCommittedPayloadBytes, "native ECS chunk commit guard reported invalid committed bytes");

    bool rejectedCreate = false;
    const std::array singlePositionValue{
        kb::ecs::NativeComponentValue{ .type = ComponentType<Position>(kPositionId), .data = &kPosition },
    };
    try {
        [[maybe_unused]] const kb::ecs::Entity rejected = storage.CreateEntity(singlePositionValue);
    } catch (const std::length_error&) {
        rejectedCreate = true;
    }
    kb::tests::Require(rejectedCreate, "native ECS chunk commit guard allowed a second committed chunk");
    kb::tests::Require(storage.Stats().liveEntities == capacity.entitiesPerChunk, "native ECS chunk commit guard leaked an entity on rejected create");

    storage.DestroyEntities(entities);
    const kb::ecs::NativeEcsStorageStats afterDestroy = storage.Stats();
    kb::tests::Require(afterDestroy.committedPayloadBytes == config.maxNativeStorageCommittedPayloadBytes, "native ECS chunk commit guard lost retained committed bytes");
    kb::tests::Require(afterDestroy.freePayloadBytes == config.maxNativeStorageCommittedPayloadBytes, "native ECS chunk commit guard did not expose reusable free bytes");

    const kb::ecs::Entity reused = storage.CreateEntity(singlePositionValue);
    kb::tests::Require(storage.IsAlive(reused), "native ECS chunk commit guard blocked free chunk reuse");
    kb::tests::Require(storage.Stats().chunkPoolSystemAllocationCount == 1U, "native ECS chunk commit guard allocated from system during reuse");

    constexpr Velocity kVelocity{ .x = 3.0F, .y = 4.0F };
    const std::array velocityValue{
        kb::ecs::NativeComponentValue{ .type = ComponentType<Velocity>(kVelocityId), .data = &kVelocity },
    };
    bool rejectedMigration = false;
    try {
        storage.AddComponents(reused, velocityValue);
    } catch (const std::length_error&) {
        rejectedMigration = true;
    }
    kb::tests::Require(rejectedMigration, "native ECS chunk commit guard allowed a migration requiring another committed chunk");
    kb::tests::Require(storage.IsAlive(reused), "native ECS chunk commit guard killed an entity on rejected migration");
    kb::tests::Require(!storage.HasComponent(reused, kVelocityId), "native ECS chunk commit guard partially migrated a rejected entity");
}

void RunGeneratedEntityResolveFastPathTest() {
    kb::ecs::NativeArchetypeStorage storage;
    Position position{ .x = 1.0F, .y = 2.0F };
    const std::array components{
        kb::ecs::NativeComponentValue{ .type = ComponentType<Position>(kPositionId), .data = &position },
    };

    const kb::ecs::Entity first = storage.CreateEntity(components);
    const kb::ecs::Entity::IdType strippedFirst = first.Id() & 0xFFFFFFFFULL;
    kb::tests::Require(storage.ResolveAliveEntity(strippedFirst) == first, "native ECS stripped id resolve missed a live generated entity");

    storage.DestroyEntity(first);
    const kb::ecs::Entity recycled = storage.CreateEntity(components);
    kb::tests::Require(recycled.Id() != first.Id(), "native ECS did not advance generation for a recycled entity");
    kb::tests::Require((recycled.Id() & 0xFFFFFFFFULL) == strippedFirst, "native ECS recycled entity did not reuse the stripped slot id");
    kb::tests::Require(!storage.IsAlive(first), "native ECS kept a stale generated entity alive");
    kb::tests::Require(storage.ResolveAliveEntity(strippedFirst) == recycled, "native ECS stripped id resolve missed the recycled generation");
}

void RunMemoryCountersPerArchetypeAndChunkTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk16KB;
    config.reserveEntities = 16;
    config.reserveArchetypes = 4;

    kb::ecs::NativeArchetypeStorage storage{ config };

    Position position{ .x = 1.0F, .y = 2.0F };
    Velocity velocity{ .x = 3.0F, .y = 4.0F };
    const std::array positionComponents{
        kb::ecs::NativeComponentValue{ .type = ComponentType<Position>(kPositionId), .data = &position },
    };
    const std::array movingComponents{
        kb::ecs::NativeComponentValue{ .type = ComponentType<Position>(kPositionId), .data = &position },
        kb::ecs::NativeComponentValue{
            .type = kb::ecs::NativeComponentType{
                .id = kVelocityId,
                .size = sizeof(Velocity),
                .alignment = alignof(Velocity),
                .storageClass = kb::ecs::ComponentStorageClass::ColdTable,
            },
            .data = &velocity,
        },
    };

    const kb::ecs::Entity positionEntity = storage.CreateEntity(positionComponents);
    const kb::ecs::Entity movingEntity = storage.CreateEntity(movingComponents);
    kb::tests::Require(storage.IsAlive(positionEntity), "native ECS memory counters setup did not create the position entity");
    kb::tests::Require(storage.IsAlive(movingEntity), "native ECS memory counters setup did not create the moving entity");

    const kb::ecs::NativeEcsStorageStats stats = storage.Stats();
    const kb::ecs::NativeEcsArchetypeMemoryCounters* positionArchetype = nullptr;
    const kb::ecs::NativeEcsArchetypeMemoryCounters* movingArchetype = nullptr;
    std::size_t countedChunks = 0;
    std::size_t countedCapacity = 0;
    std::size_t countedSparseChunks = 0;
    std::size_t countedTailSparseChunks = 0;
    std::size_t countedFragmentedChunks = 0;
    std::size_t countedEmptyChunks = 0;
    std::size_t countedUsedBytes = 0;
    std::size_t countedWastedBytes = 0;
    std::size_t countedMetadataBytes = 0;

    for (const kb::ecs::NativeEcsArchetypeMemoryCounters& archetype : stats.archetypeCounters) {
        if (archetype.componentIds.size() == 1U && archetype.componentIds[0] == kPositionId) {
            positionArchetype = &archetype;
        }
        if (archetype.componentIds.size() == 2U && archetype.componentIds[0] == kPositionId && archetype.componentIds[1] == kVelocityId) {
            movingArchetype = &archetype;
        }

        kb::tests::Require(archetype.chunkCounters.size() == archetype.chunks, "native ECS memory counters omitted chunk counters");
        kb::tests::Require(archetype.capacity >= archetype.liveEntities, "native ECS memory counters reported invalid archetype capacity");
        kb::tests::Require(archetype.usedBytes + archetype.wastedBytes == archetype.payloadBytes, "native ECS memory counters reported invalid archetype byte totals");
        kb::tests::Require(archetype.version > 0U, "native ECS memory counters omitted archetype version");
        kb::tests::Require(
            archetype.hotTableComponents + archetype.coldTableComponents + archetype.sparseTagComponents + archetype.sparsePayloadComponents
                + archetype.sharedValueComponents + archetype.externalBlobComponents == archetype.componentIds.size(),
            "native ECS memory counters reported invalid storage-class component totals");
        kb::tests::Require(
            archetype.hotTableComponents + archetype.coldTableComponents + archetype.sparseTagComponents + archetype.sparsePayloadComponents
                + archetype.sharedValueComponents + archetype.externalBlobComponents == archetype.componentIds.size(),
            "native ECS memory counters reported invalid storage-class component totals");

        std::size_t archetypeRows = 0;
        std::size_t archetypeUsedBytes = 0;
        std::size_t archetypeWastedBytes = 0;
        std::size_t archetypePayloadBytes = 0;
        std::size_t archetypeMetadataBytes = 0;
        for (const kb::ecs::NativeEcsChunkMemoryCounters& chunk : archetype.chunkCounters) {
            kb::tests::Require(chunk.archetypeIndex == archetype.archetypeIndex, "native ECS memory counters recorded invalid chunk archetype index");
            kb::tests::Require(chunk.capacity >= chunk.liveEntities, "native ECS memory counters reported invalid chunk capacity");
            kb::tests::Require(
                chunk.payloadBytes == storage.ChunkPayloadBytes() + chunk.sidePayloadBytes,
                "native ECS memory counters recorded invalid chunk payload bytes");
            kb::tests::Require(chunk.usedBytes + chunk.wastedBytes == chunk.payloadBytes, "native ECS memory counters reported invalid chunk byte totals");
            kb::tests::Require(chunk.metadataBytes >= chunk.capacity * sizeof(kb::ecs::Entity), "native ECS memory counters missed chunk entity metadata");
            if (chunk.liveEntities == 0U) {
                ++countedEmptyChunks;
            } else if (chunk.liveEntities < chunk.capacity) {
                ++countedSparseChunks;
                if (chunk.chunkIndex + 1U == archetype.chunkCounters.size()) {
                    ++countedTailSparseChunks;
                } else {
                    ++countedFragmentedChunks;
                }
            }
            archetypeRows += chunk.liveEntities;
            archetypeUsedBytes += chunk.usedBytes;
            archetypeWastedBytes += chunk.wastedBytes;
            archetypePayloadBytes += chunk.payloadBytes;
            archetypeMetadataBytes += chunk.metadataBytes;
        }

        kb::tests::Require(archetypeRows == archetype.liveEntities, "native ECS memory counters did not sum chunk entity counts");
        kb::tests::Require(archetypeUsedBytes == archetype.usedBytes, "native ECS memory counters did not sum chunk used bytes");
        kb::tests::Require(archetypeWastedBytes == archetype.wastedBytes, "native ECS memory counters did not sum chunk wasted bytes");
        kb::tests::Require(archetypePayloadBytes == archetype.payloadBytes, "native ECS memory counters did not sum chunk payload bytes");
        kb::tests::Require(archetypeMetadataBytes == archetype.metadataBytes, "native ECS memory counters did not sum chunk metadata bytes");

        countedChunks += archetype.chunks;
        countedCapacity += archetype.capacity;
        countedUsedBytes += archetype.usedBytes;
        countedWastedBytes += archetype.wastedBytes;
        countedMetadataBytes += archetype.metadataBytes;
    }

    kb::tests::Require(stats.archetypeCounters.size() == stats.archetypeCount, "native ECS memory counters omitted archetype counters");
    kb::tests::Require(stats.chunks == countedChunks, "native ECS memory counters did not sum storage chunk count");
    kb::tests::Require(stats.capacity == countedCapacity, "native ECS memory counters did not sum storage capacity");
    kb::tests::Require(stats.sparseChunks == countedSparseChunks, "native ECS memory counters did not sum sparse chunk count");
    kb::tests::Require(stats.tailSparseChunks == countedTailSparseChunks, "native ECS memory counters did not sum tail sparse chunk count");
    kb::tests::Require(stats.fragmentedChunks == countedFragmentedChunks, "native ECS memory counters did not sum fragmented chunk count");
    kb::tests::Require(stats.emptyChunks == countedEmptyChunks, "native ECS memory counters did not sum empty chunk count");
    kb::tests::Require(stats.fragmentedChunks == 0U, "native ECS storage left an interior sparse chunk after compaction");
    kb::tests::Require(stats.emptyChunks == 0U, "native ECS storage left an empty chunk in use after compaction");
    kb::tests::Require(stats.usedBytes == countedUsedBytes, "native ECS memory counters did not sum storage used bytes");
    kb::tests::Require(stats.wastedBytes == countedWastedBytes, "native ECS memory counters did not sum storage wasted bytes");
    kb::tests::Require(stats.chunkMetadataBytes == countedMetadataBytes, "native ECS memory counters did not sum storage metadata bytes");
    kb::tests::Require(stats.entityRecordBytes >= stats.liveEntities * sizeof(kb::ecs::Entity), "native ECS memory counters reported invalid entity record bytes");
    kb::tests::Require(
        stats.trackedBytes == stats.committedPayloadBytes + stats.activeSidePayloadBytes + stats.chunkMetadataBytes + stats.entityRecordBytes,
        "native ECS memory counters reported invalid tracked bytes");
    kb::tests::Require(positionArchetype != nullptr && positionArchetype->usedBytes == sizeof(Position), "native ECS memory counters missed the position archetype");
    kb::tests::Require(movingArchetype != nullptr && movingArchetype->usedBytes == sizeof(Position) + sizeof(Velocity), "native ECS memory counters missed the moving archetype");
    kb::tests::Require(positionArchetype->hotOnlyCapacity == positionArchetype->capacity, "native ECS memory counters reported hot-only capacity loss for an all-hot archetype");
    kb::tests::Require(positionArchetype->capacityLostToNonHotStorage == 0U, "native ECS memory counters reported non-hot capacity loss for an all-hot archetype");
    kb::tests::Require(movingArchetype->hotOnlyCapacity == movingArchetype->capacity, "native ECS memory counters did not recover hot-only capacity for a mixed archetype");
    kb::tests::Require(
        movingArchetype->capacityLostToNonHotStorage > 0U,
        "native ECS memory counters missed avoided non-hot capacity loss");
    kb::tests::Require(movingArchetype->sidePayloadBytes > 0U, "native ECS memory counters missed mixed archetype side payload");
    kb::tests::Require(stats.activeSidePayloadBytes >= movingArchetype->sidePayloadBytes, "native ECS memory counters missed aggregate side payload");
    kb::tests::Require(stats.hotOnlyCapacity >= stats.capacity, "native ECS memory counters reported invalid aggregate hot-only capacity");
    kb::tests::Require(stats.capacityLostToNonHotStorage >= movingArchetype->capacityLostToNonHotStorage, "native ECS memory counters missed aggregate non-hot capacity loss");
    kb::tests::Require(positionArchetype->hotTableComponents == 1U, "native ECS memory counters missed hot table storage metadata");
    kb::tests::Require(movingArchetype->hotTableComponents == 1U, "native ECS memory counters missed moving hot table storage metadata");
    kb::tests::Require(movingArchetype->coldTableComponents == 1U, "native ECS memory counters missed cold table storage metadata");
    kb::tests::Require(stats.hotTableComponents == 2U, "native ECS memory counters reported invalid aggregate hot table metadata");
    kb::tests::Require(stats.coldTableComponents == 1U, "native ECS memory counters reported invalid aggregate cold table metadata");
    kb::tests::Require(positionArchetype->hotTableUsedBytes == sizeof(Position), "native ECS memory counters reported invalid hot table used bytes");
    kb::tests::Require(movingArchetype->hotTableUsedBytes == sizeof(Position), "native ECS memory counters reported invalid moving hot table used bytes");
    kb::tests::Require(movingArchetype->coldTableUsedBytes == sizeof(Velocity), "native ECS memory counters reported invalid cold table used bytes");
    kb::tests::Require(stats.hotTableUsedBytes == sizeof(Position) * 2U, "native ECS memory counters reported invalid aggregate hot table used bytes");
    kb::tests::Require(stats.coldTableUsedBytes == sizeof(Velocity), "native ECS memory counters reported invalid aggregate cold table used bytes");
    kb::tests::Require(
        positionArchetype->hotTableCapacityBytes == positionArchetype->capacity * sizeof(Position),
        "native ECS memory counters reported invalid hot table capacity bytes");
    kb::tests::Require(
        movingArchetype->coldTableCapacityBytes == movingArchetype->capacity * sizeof(Velocity),
        "native ECS memory counters reported invalid cold table capacity bytes");
}

void RunStructuralChurnOccupancyTest() {
    // Roadmap P5 acceptance: after random structural churn, move-last compaction
    // must keep occupancy above 85% without an explicit defrag pass (only the
    // tail chunk is ever permitted to be sparse).
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk16KB;
    config.reserveEntities = 8192;
    kb::ecs::NativeArchetypeStorage storage{ config };

    constexpr std::size_t kEntityCount = 20000U;
    std::vector<Position> positions;
    positions.reserve(kEntityCount);
    for (std::size_t index = 0; index < kEntityCount; ++index) {
        positions.push_back(Position{ .x = static_cast<float>(index), .y = static_cast<float>(index) });
    }
    const std::array columns{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Position>(kPositionId),
            .data = positions.data(),
            .stride = sizeof(Position),
            .sourceCount = positions.size(),
        },
    };
    std::vector<kb::ecs::Entity> entities = storage.CreateEntities(kEntityCount, std::span<const kb::ecs::NativeBulkComponentColumn>{ columns });

    // Deterministic pseudo-random churn: destroy ~40% via a fixed-seed walk.
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    const auto nextRandom = [&state]() noexcept {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    std::size_t destroyed = 0U;
    for (kb::ecs::Entity entity : entities) {
        if ((nextRandom() % 5U) < 2U) {
            storage.DestroyEntity(entity);
            ++destroyed;
        }
    }

    const kb::ecs::NativeEcsStorageStats stats = storage.Stats();
    kb::tests::Require(stats.liveEntities == kEntityCount - destroyed, "structural churn occupancy test lost live entity accounting");
    kb::tests::Require(stats.capacity > 0U, "structural churn occupancy test reported zero capacity");
    const double occupancy = static_cast<double>(stats.liveEntities) / static_cast<double>(stats.capacity);
    kb::tests::Require(occupancy > 0.85, "structural churn left occupancy below the 85% compaction target");
    kb::tests::Require(stats.sparseChunks <= 1U, "structural churn left more than the tail chunk sparse");
}

void RunMultiComponentMigrationTest() {
    kb::ecs::NativeArchetypeStorage storage;

    Position position{ .x = 1.0F, .y = 2.0F };
    Velocity velocity{ .x = 0.5F, .y = 1.5F };
    const std::array initial{
        kb::ecs::NativeComponentValue{ .type = ComponentType<Position>(kPositionId), .data = &position },
        kb::ecs::NativeComponentValue{ .type = ComponentType<Velocity>(kVelocityId), .data = &velocity },
    };
    const kb::ecs::Entity entity = storage.CreateEntity(initial);
    const std::uint64_t initialArchetypeVersion = storage.ArchetypeVersion(entity);
    const std::uint64_t initialPositionVersion = storage.ComponentVersion(entity, kPositionId);

    Mass mass{ .value = 7.0F };
    RuntimeTag tag{ .value = 9U };
    const std::array added{
        kb::ecs::NativeComponentValue{ .type = ComponentType<Mass>(kMassId), .data = &mass },
        kb::ecs::NativeComponentValue{ .type = ComponentType<RuntimeTag>(kRuntimeTagId), .data = &tag },
    };
    storage.AddComponents(entity, added);

    kb::tests::Require(storage.HasComponent(entity, kPositionId), "native ECS migration lost an existing component");
    kb::tests::Require(storage.HasComponent(entity, kVelocityId), "native ECS migration lost a second existing component");
    kb::tests::Require(storage.HasComponent(entity, kMassId), "native ECS migration did not add the first component");
    kb::tests::Require(storage.HasComponent(entity, kRuntimeTagId), "native ECS migration did not add the second component");
    kb::tests::Require(kb::tests::NearlyEqual(Component<Position>(storage, entity, kPositionId).x, 1.0F), "native ECS migration corrupted position data");
    kb::tests::Require(kb::tests::NearlyEqual(Component<Velocity>(storage, entity, kVelocityId).y, 1.5F), "native ECS migration corrupted velocity data");
    kb::tests::Require(kb::tests::NearlyEqual(Component<Mass>(storage, entity, kMassId).value, 7.0F), "native ECS migration wrote invalid mass data");
    kb::tests::Require(Component<RuntimeTag>(storage, entity, kRuntimeTagId).value == 9U, "native ECS migration wrote invalid tag data");
    kb::tests::Require(storage.ArchetypeVersion(entity) > initialArchetypeVersion, "native ECS archetype version did not advance after migration");
    kb::tests::Require(storage.ComponentVersion(entity, kPositionId) >= initialPositionVersion, "native ECS component version regressed after migration");

    const std::array removed{ kVelocityId, kRuntimeTagId };
    storage.RemoveComponents(entity, removed);
    kb::tests::Require(storage.HasComponent(entity, kPositionId), "native ECS remove migration lost a retained component");
    kb::tests::Require(storage.HasComponent(entity, kMassId), "native ECS remove migration lost added mass");
    kb::tests::Require(!storage.HasComponent(entity, kVelocityId), "native ECS remove migration retained removed velocity");
    kb::tests::Require(!storage.HasComponent(entity, kRuntimeTagId), "native ECS remove migration retained removed tag");
    kb::tests::Require(kb::tests::NearlyEqual(Component<Position>(storage, entity, kPositionId).y, 2.0F), "native ECS remove migration corrupted position data");
    kb::tests::Require(kb::tests::NearlyEqual(Component<Mass>(storage, entity, kMassId).value, 7.0F), "native ECS remove migration corrupted mass data");
}

void RunBulkRemoveComponentMigrationTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk4KB;
    config.reserveEntities = 128;
    config.reserveArchetypes = 4;
    kb::ecs::NativeArchetypeStorage storage{ config };

    constexpr std::size_t kEntityCount = 96U;
    std::vector<Position> positions;
    std::vector<Velocity> velocities;
    std::vector<Mass> masses;
    positions.reserve(kEntityCount);
    velocities.reserve(kEntityCount);
    masses.reserve(kEntityCount);
    for (std::size_t index = 0; index < kEntityCount; ++index) {
        positions.push_back(Position{ .x = static_cast<float>(index + 1U), .y = static_cast<float>(index + 100U) });
        velocities.push_back(Velocity{ .x = static_cast<float>(index + 200U), .y = static_cast<float>(index + 300U) });
        masses.push_back(Mass{ .value = static_cast<float>(index + 400U) });
    }

    const std::array columns{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Position>(kPositionId),
            .data = positions.data(),
            .stride = sizeof(Position),
        },
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Velocity>(kVelocityId),
            .data = velocities.data(),
            .stride = sizeof(Velocity),
        },
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Mass>(kMassId),
            .data = masses.data(),
            .stride = sizeof(Mass),
        },
    };
    const std::vector<kb::ecs::Entity> entities = storage.CreateEntities(kEntityCount, columns);

    std::vector<kb::ecs::Entity> migrated;
    migrated.reserve(kEntityCount / 2U);
    for (std::size_t index = 0; index < entities.size(); index += 2U) {
        migrated.push_back(entities[index]);
    }

    const std::array removed{ kVelocityId };
    storage.RemoveComponents(std::span<const kb::ecs::Entity>{ migrated }, std::span<const kb::ecs::ComponentId>{ removed });

    const std::array retainedSignature{ kPositionId, kMassId };
    const std::array fullSignature{ kPositionId, kVelocityId, kMassId };
    for (std::size_t index = 0; index < entities.size(); ++index) {
        const kb::ecs::Entity entity = entities[index];
        kb::tests::Require(storage.IsAlive(entity), "native ECS bulk remove migration destroyed an entity");
        kb::tests::Require(storage.HasComponent(entity, kPositionId), "native ECS bulk remove migration lost position");
        kb::tests::Require(storage.HasComponent(entity, kMassId), "native ECS bulk remove migration lost mass");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Position>(storage, entity, kPositionId).x, positions[index].x), "native ECS bulk remove migration corrupted position data");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Mass>(storage, entity, kMassId).value, masses[index].value), "native ECS bulk remove migration corrupted mass data");

        if ((index % 2U) == 0U) {
            kb::tests::Require(!storage.HasComponent(entity, kVelocityId), "native ECS bulk remove migration retained removed velocity");
            kb::tests::Require(storage.EntityArchetypeMatches(entity, retainedSignature), "native ECS bulk remove migration assigned an invalid retained archetype");
            kb::tests::Require(!storage.EntityArchetypeMatches(entity, fullSignature), "native ECS bulk remove migration kept the full archetype signature");
        } else {
            kb::tests::Require(storage.HasComponent(entity, kVelocityId), "native ECS bulk remove migration changed an untouched entity");
            kb::tests::Require(storage.EntityArchetypeMatches(entity, fullSignature), "native ECS bulk remove migration corrupted an untouched archetype");
            kb::tests::Require(kb::tests::NearlyEqual(Component<Velocity>(storage, entity, kVelocityId).y, velocities[index].y), "native ECS bulk remove migration corrupted untouched velocity data");
        }
    }

    RequireStorageStatsConsistent(storage.Stats(), kEntityCount, "native ECS bulk remove migration storage stats are inconsistent");
}

void RunBulkStructuralScratchReuseTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk4KB;
    config.reserveEntities = 96;
    config.reserveArchetypes = 6;
    kb::ecs::NativeArchetypeStorage storage{ config };

    constexpr std::size_t kEntityCount = 48U;
    std::vector<Position> positions;
    positions.reserve(kEntityCount);
    for (std::size_t index = 0; index < kEntityCount; ++index) {
        positions.push_back(Position{ .x = static_cast<float>(index + 10U), .y = static_cast<float>(index + 20U) });
    }

    const std::array positionColumn{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Position>(kPositionId),
            .data = positions.data(),
            .stride = sizeof(Position),
        },
    };
    const std::vector<kb::ecs::Entity> entities = storage.CreateEntities(kEntityCount, positionColumn);

    std::vector<kb::ecs::Entity> evenEntities;
    std::vector<kb::ecs::Entity> oddEntities;
    std::vector<Velocity> velocities;
    std::vector<Mass> masses;
    evenEntities.reserve(kEntityCount / 2U);
    oddEntities.reserve(kEntityCount / 2U);
    velocities.reserve(kEntityCount / 2U);
    masses.reserve(kEntityCount / 2U);
    for (std::size_t index = 0; index < entities.size(); ++index) {
        if ((index % 2U) == 0U) {
            evenEntities.push_back(entities[index]);
            velocities.push_back(Velocity{ .x = static_cast<float>(index + 100U), .y = static_cast<float>(index + 200U) });
        } else {
            oddEntities.push_back(entities[index]);
            masses.push_back(Mass{ .value = static_cast<float>(index + 300U) });
        }
    }

    const std::array velocityColumn{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Velocity>(kVelocityId),
            .data = velocities.data(),
            .stride = sizeof(Velocity),
        },
    };
    storage.AddComponents(std::span<const kb::ecs::Entity>{ evenEntities }, velocityColumn);

    const std::array massColumn{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Mass>(kMassId),
            .data = masses.data(),
            .stride = sizeof(Mass),
        },
    };
    storage.AddComponents(std::span<const kb::ecs::Entity>{ oddEntities }, massColumn);

    std::vector<Mass> updatedMasses;
    updatedMasses.reserve(oddEntities.size());
    for (std::size_t index = 0; index < oddEntities.size(); ++index) {
        updatedMasses.push_back(Mass{ .value = static_cast<float>(index + 900U) });
    }
    const std::array updatedMassColumn{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Mass>(kMassId),
            .data = updatedMasses.data(),
            .stride = sizeof(Mass),
        },
    };
    storage.SetComponents(std::span<const kb::ecs::Entity>{ oddEntities }, updatedMassColumn);

    const std::array removedVelocity{ kVelocityId };
    storage.RemoveComponents(std::span<const kb::ecs::Entity>{ evenEntities }, std::span<const kb::ecs::ComponentId>{ removedVelocity });

    const std::array destroyed{
        entities[0],
        entities[1],
        entities[2],
        entities[3],
        entities[4],
        entities[5],
    };
    storage.DestroyEntities(destroyed);

    for (kb::ecs::Entity entity : destroyed) {
        kb::tests::Require(!storage.IsAlive(entity), "native ECS bulk scratch reuse kept a destroyed entity alive");
    }

    for (std::size_t index = 6U; index < entities.size(); ++index) {
        const kb::ecs::Entity entity = entities[index];
        kb::tests::Require(storage.IsAlive(entity), "native ECS bulk scratch reuse invalidated a survivor");
        kb::tests::Require(storage.HasComponent(entity, kPositionId), "native ECS bulk scratch reuse lost position");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Position>(storage, entity, kPositionId).x, positions[index].x), "native ECS bulk scratch reuse corrupted position");
        if ((index % 2U) == 0U) {
            kb::tests::Require(!storage.HasComponent(entity, kVelocityId), "native ECS bulk scratch reuse retained removed velocity");
            kb::tests::Require(!storage.HasComponent(entity, kMassId), "native ECS bulk scratch reuse added mass to an even entity");
        } else {
            const std::size_t oddRow = index / 2U;
            kb::tests::Require(storage.HasComponent(entity, kMassId), "native ECS bulk scratch reuse lost mass");
            kb::tests::Require(kb::tests::NearlyEqual(Component<Mass>(storage, entity, kMassId).value, updatedMasses[oddRow].value), "native ECS bulk scratch reuse corrupted mass");
            kb::tests::Require(!storage.HasComponent(entity, kVelocityId), "native ECS bulk scratch reuse added velocity to an odd entity");
        }
    }

    RequireStorageStatsConsistent(storage.Stats(), kEntityCount - destroyed.size(), "native ECS bulk scratch reuse left inconsistent storage stats");
}

void RunBulkColumnSourceCountValidationTest() {
    kb::ecs::NativeArchetypeStorage storage;

    constexpr std::size_t kEntityCount = 4U;
    std::vector<Position> positions;
    positions.reserve(kEntityCount);
    for (std::size_t index = 0; index < kEntityCount; ++index) {
        positions.push_back(Position{ .x = static_cast<float>(index + 1U), .y = static_cast<float>(index + 11U) });
    }
    const std::array positionColumn{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Position>(kPositionId),
            .data = positions.data(),
        },
    };
    const std::vector<kb::ecs::Entity> entities = storage.CreateEntities(kEntityCount, positionColumn);
    RequireStorageStatsConsistent(storage.Stats(), kEntityCount, "native ECS bulk source count setup stats are inconsistent");

    const std::array invalidCreateColumn{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Velocity>(kVelocityId),
            .data = positions.data(),
            .sourceCount = 3U,
        },
    };
    bool rejectedInvalidCreate = false;
    try {
        static_cast<void>(storage.CreateEntities(kEntityCount, invalidCreateColumn));
    } catch (const std::invalid_argument&) {
        rejectedInvalidCreate = true;
    }
    kb::tests::Require(rejectedInvalidCreate, "native ECS bulk create accepted a non-dividing source count");
    RequireStorageStatsConsistent(storage.Stats(), kEntityCount, "native ECS invalid bulk create changed storage stats");

    const std::array patternMasses{
        Mass{ .value = 3.0F },
        Mass{ .value = 7.0F },
    };
    const std::array invalidAddColumn{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Mass>(kMassId),
            .data = patternMasses.data(),
            .stride = sizeof(Mass),
            .sourceCount = patternMasses.size(),
        },
    };
    bool rejectedInvalidAdd = false;
    try {
        storage.AddComponents(std::span<const kb::ecs::Entity>{ entities }, invalidAddColumn);
    } catch (const std::invalid_argument&) {
        rejectedInvalidAdd = true;
    }
    kb::tests::Require(rejectedInvalidAdd, "native ECS bulk add accepted a row-mapped pattern source count");
    for (kb::ecs::Entity entity : entities) {
        kb::tests::Require(!storage.HasComponent(entity, kMassId), "native ECS invalid bulk add partially migrated an entity");
    }
    RequireStorageStatsConsistent(storage.Stats(), kEntityCount, "native ECS invalid bulk add changed storage stats");

    const Mass broadcastMass{ .value = 13.0F };
    const std::array broadcastAddColumn{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Mass>(kMassId),
            .data = &broadcastMass,
            .sourceCount = 1U,
        },
    };
    storage.AddComponents(std::span<const kb::ecs::Entity>{ entities }, broadcastAddColumn);
    for (kb::ecs::Entity entity : entities) {
        kb::tests::Require(kb::tests::NearlyEqual(Component<Mass>(storage, entity, kMassId).value, broadcastMass.value), "native ECS bulk add did not broadcast a single source row");
    }

    std::array<Mass, kEntityCount> updatedMasses{};
    for (std::size_t index = 0; index < updatedMasses.size(); ++index) {
        updatedMasses[index].value = static_cast<float>(index + 20U);
    }
    const std::array fullSetColumn{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Mass>(kMassId),
            .data = updatedMasses.data(),
        },
    };
    storage.SetComponents(std::span<const kb::ecs::Entity>{ entities }, fullSetColumn);
    for (std::size_t index = 0; index < entities.size(); ++index) {
        kb::tests::Require(kb::tests::NearlyEqual(Component<Mass>(storage, entities[index], kMassId).value, updatedMasses[index].value), "native ECS full bulk set was treated as a broadcast");
    }
}

void RunSwapDeleteAndGenerationTest() {
    kb::ecs::NativeArchetypeStorage storage;

    Position firstPosition{ .x = 1.0F, .y = 10.0F };
    Position secondPosition{ .x = 2.0F, .y = 20.0F };
    Position thirdPosition{ .x = 3.0F, .y = 30.0F };

    const kb::ecs::NativeComponentType positionType = ComponentType<Position>(kPositionId);
    const std::array firstComponents{ kb::ecs::NativeComponentValue{ .type = positionType, .data = &firstPosition } };
    const std::array secondComponents{ kb::ecs::NativeComponentValue{ .type = positionType, .data = &secondPosition } };
    const std::array thirdComponents{ kb::ecs::NativeComponentValue{ .type = positionType, .data = &thirdPosition } };
    const kb::ecs::Entity first = storage.CreateEntity(firstComponents);
    const kb::ecs::Entity second = storage.CreateEntity(secondComponents);
    const kb::ecs::Entity third = storage.CreateEntity(thirdComponents);

    storage.DestroyEntity(second);
    kb::tests::Require(storage.IsAlive(first), "native ECS swap-delete invalidated the first entity");
    kb::tests::Require(!storage.IsAlive(second), "native ECS generation validation kept a destroyed entity alive");
    kb::tests::Require(storage.IsAlive(third), "native ECS swap-delete invalidated the moved entity");
    kb::tests::Require(kb::tests::NearlyEqual(Component<Position>(storage, third, kPositionId).x, 3.0F), "native ECS swap-delete corrupted moved component data");

    Position replacementPosition{ .x = 4.0F, .y = 40.0F };
    const std::array replacementComponents{ kb::ecs::NativeComponentValue{ .type = positionType, .data = &replacementPosition } };
    const kb::ecs::Entity replacement = storage.CreateEntity(replacementComponents);
    kb::tests::Require(replacement.Id() != second.Id(), "native ECS reused a stale entity handle without advancing generation");
    kb::tests::Require(storage.IsAlive(replacement), "native ECS did not reuse a freed entity slot as a live handle");
    kb::tests::Require(kb::tests::NearlyEqual(Component<Position>(storage, replacement, kPositionId).y, 40.0F), "native ECS wrote invalid replacement component data");
}

void RunMoveLastCompactionAcrossChunksTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk4KB;
    config.reserveEntities = 2048;

    kb::ecs::NativeArchetypeStorage storage{ config };
    constexpr std::size_t kEntityCount = 1025U;
    std::vector<Position> positions;
    positions.reserve(kEntityCount);
    for (std::size_t index = 0; index < kEntityCount; ++index) {
        positions.push_back(Position{ .x = static_cast<float>(index), .y = static_cast<float>(index + 10U) });
    }

    const std::array columns{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Position>(kPositionId),
            .data = positions.data(),
            .stride = sizeof(Position),
            .sourceCount = positions.size(),
        },
    };
    const std::vector<kb::ecs::Entity> entities = storage.CreateEntities(kEntityCount, std::span<const kb::ecs::NativeBulkComponentColumn>{ columns });

    const kb::ecs::NativeEcsStorageStats before = storage.Stats();
    kb::tests::Require(before.chunks == 3U, "native ECS move-last compaction setup did not allocate three chunks");
    kb::tests::Require(before.sparseChunks == 1U && before.tailSparseChunks == 1U, "native ECS move-last compaction setup did not create a sparse tail chunk");

    const kb::ecs::Entity first = entities.front();
    const kb::ecs::Entity last = entities.back();
    storage.DestroyEntity(first);

    kb::tests::Require(!storage.IsAlive(first), "native ECS move-last compaction kept the destroyed entity alive");
    kb::tests::Require(storage.IsAlive(last), "native ECS move-last compaction invalidated the moved tail entity");
    kb::tests::Require(kb::tests::NearlyEqual(Component<Position>(storage, last, kPositionId).x, positions.back().x), "native ECS move-last compaction corrupted moved component x");
    kb::tests::Require(kb::tests::NearlyEqual(Component<Position>(storage, last, kPositionId).y, positions.back().y), "native ECS move-last compaction corrupted moved component y");

    std::vector<kb::ecs::QueryTableDispatchRecord> records;
    const std::array queryIds{ kPositionId };
    storage.CollectQueryRecords(queryIds, {}, {}, records);
    kb::tests::Require(records.size() == 2U, "native ECS move-last compaction did not release the empty tail chunk");
    kb::tests::Require(records.front().entityIds != nullptr && records.front().entityIds[0] == last.Id(), "native ECS move-last compaction did not move the tail entity into the removed row");

    const kb::ecs::NativeEcsStorageStats after = storage.Stats();
    kb::tests::Require(after.liveEntities == kEntityCount - 1U, "native ECS move-last compaction reported invalid live entity count");
    kb::tests::Require(after.chunks == 2U, "native ECS move-last compaction left an empty chunk in use");
    kb::tests::Require(after.sparseChunks == 0U, "native ECS move-last compaction left sparse chunks after exact refill");
    kb::tests::Require(after.fragmentedChunks == 0U, "native ECS move-last compaction left fragmented chunks");
    kb::tests::Require(after.emptyChunks == 0U, "native ECS move-last compaction left empty chunks");
}

void RunArchetypeSignatureMatchingTest() {
    kb::ecs::NativeArchetypeStorage storage;

    Position position{ .x = 1.0F, .y = 2.0F };
    Velocity velocity{ .x = 3.0F, .y = 4.0F };
    Mass mass{ .value = 5.0F };

    const std::array positionOnlyComponents{
        kb::ecs::NativeComponentValue{ .type = ComponentType<Position>(kPositionId), .data = &position },
    };
    const std::array movingComponents{
        kb::ecs::NativeComponentValue{ .type = ComponentType<Position>(kPositionId), .data = &position },
        kb::ecs::NativeComponentValue{
            .type = kb::ecs::NativeComponentType{
                .id = kVelocityId,
                .size = sizeof(Velocity),
                .alignment = alignof(Velocity),
                .storageClass = kb::ecs::ComponentStorageClass::ColdTable,
            },
            .data = &velocity,
        },
    };
    const std::array weightedComponents{
        kb::ecs::NativeComponentValue{ .type = ComponentType<Velocity>(kVelocityId), .data = &velocity },
        kb::ecs::NativeComponentValue{ .type = ComponentType<Mass>(kMassId), .data = &mass },
    };

    const kb::ecs::Entity positionOnly = storage.CreateEntity(positionOnlyComponents);
    const kb::ecs::Entity movingA = storage.CreateEntity(movingComponents);
    const kb::ecs::Entity movingB = storage.CreateEntity(movingComponents);
    const kb::ecs::Entity weighted = storage.CreateEntity(weightedComponents);

    const std::array positionQuery{ kPositionId };
    const std::array movingQuery{ kPositionId, kVelocityId };
    const std::array massQuery{ kMassId };
    const std::array missingQuery{ kRuntimeTagId };

    const std::vector<kb::ecs::NativeArchetypeMatch> positionMatches = storage.MatchingArchetypes(positionQuery);
    const std::vector<kb::ecs::NativeArchetypeMatch> movingMatches = storage.MatchingArchetypes(movingQuery);
    const std::vector<kb::ecs::NativeArchetypeMatch> massMatches = storage.MatchingArchetypes(massQuery);
    const std::vector<kb::ecs::NativeArchetypeMatch> missingMatches = storage.MatchingArchetypes(missingQuery);

    kb::tests::Require(positionMatches.size() == 2, "native ECS signature matching missed a position archetype");
    kb::tests::Require(movingMatches.size() == 1 && movingMatches.front().liveEntities == 2, "native ECS signature matching did not isolate moving archetypes");
    kb::tests::Require(massMatches.size() == 1 && massMatches.front().liveEntities == 1, "native ECS signature matching missed mass archetypes");
    kb::tests::Require(missingMatches.empty(), "native ECS signature matching accepted an unknown component");

    kb::tests::Require(storage.EntityArchetypeMatches(movingA, movingQuery), "native ECS entity archetype signature did not match required components");
    kb::tests::Require(storage.EntityArchetypeMatches(movingB, positionQuery), "native ECS entity archetype signature did not match subset components");
    kb::tests::Require(!storage.EntityArchetypeMatches(positionOnly, movingQuery), "native ECS entity archetype signature accepted a missing component");
    kb::tests::Require(storage.EntityArchetypeMatches(weighted, massQuery), "native ECS entity archetype signature rejected a present component");
    kb::tests::Require(storage.EntityArchetypeMatches(movingA, positionQuery, massQuery), "native ECS include/exclude signature rejected a valid moving archetype");
    kb::tests::Require(!storage.EntityArchetypeMatches(movingA, positionQuery, movingQuery), "native ECS include/exclude signature accepted an excluded component");
    kb::tests::Require(
        !storage.EntityArchetypeMatches(positionOnly, missingQuery, std::span<const kb::ecs::ComponentId>{}),
        "native ECS include/exclude signature accepted an unknown required component");
    kb::tests::Require(storage.EntityArchetypeMatches(positionOnly, positionQuery, missingQuery), "native ECS include/exclude signature rejected an unknown excluded component");

    const std::array massComponent{
        kb::ecs::NativeComponentValue{ .type = ComponentType<Mass>(kMassId), .data = &mass },
    };
    storage.AddComponents(positionOnly, massComponent);
    const std::array positionMassQuery{ kPositionId, kMassId };
    kb::tests::Require(storage.EntityArchetypeMatches(positionOnly, positionMassQuery), "native ECS signature was not updated after add-component migration");
    kb::tests::Require(!storage.EntityArchetypeMatches(positionOnly, positionQuery, massQuery), "native ECS include/exclude signature missed add-component migration");

    const std::array removedMass{ kMassId };
    storage.RemoveComponents(positionOnly, removedMass);
    kb::tests::Require(!storage.EntityArchetypeMatches(positionOnly, positionMassQuery), "native ECS signature was not updated after remove-component migration");
    kb::tests::Require(storage.EntityArchetypeMatches(positionOnly, positionQuery, massQuery), "native ECS include/exclude signature missed remove-component migration");
}

void RunNativeBulkCreateAdoptColumnAppendTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk16KB;
    kb::ecs::NativeArchetypeStorage storage{ config };

    constexpr std::size_t kEntityCount = 1300U;
    std::vector<Position> positions;
    std::vector<Velocity> velocities;
    positions.reserve(kEntityCount);
    velocities.reserve(kEntityCount);
    for (std::size_t index = 0; index < kEntityCount; ++index) {
        positions.push_back(Position{ .x = static_cast<float>(index + 1U), .y = static_cast<float>(index + 2U) });
        velocities.push_back(Velocity{ .x = static_cast<float>(index + 3U), .y = static_cast<float>(index + 4U) });
    }

    const std::array columns{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Position>(kPositionId),
            .data = positions.data(),
            .stride = sizeof(Position),
        },
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Velocity>(kVelocityId),
            .data = velocities.data(),
            .stride = sizeof(Velocity),
        },
    };

    const std::vector<kb::ecs::Entity> entities = storage.CreateEntities(kEntityCount, columns);
    kb::tests::Require(entities.size() == kEntityCount, "native ECS bulk create did not return the requested entity count");
    kb::tests::Require(storage.Stats().chunks > 1U, "native ECS bulk create did not append across multiple chunks");

    const std::array movingQuery{ kPositionId, kVelocityId };
    std::size_t dirtyPositionRows = 0U;
    std::size_t dirtyVelocityRows = 0U;
    std::vector<kb::ecs::QueryTableDispatchRecord> createdRecords;
    storage.CollectQueryRecords(movingQuery, {}, {}, createdRecords);
    kb::tests::Require(createdRecords.size() > 1U, "native ECS bulk create dirty regression setup did not span multiple query records");
    for (const kb::ecs::QueryTableDispatchRecord& record : createdRecords) {
        kb::tests::Require(record.componentDirtyCounts[0] == record.entityCount, "native ECS bulk create did not dirty all position rows in a chunk");
        kb::tests::Require(record.componentDirtyCounts[1] == record.entityCount, "native ECS bulk create did not dirty all velocity rows in a chunk");
        dirtyPositionRows += record.componentDirtyCounts[0];
        dirtyVelocityRows += record.componentDirtyCounts[1];
    }
    kb::tests::Require(dirtyPositionRows == kEntityCount, "native ECS bulk create dirty position row total is invalid");
    kb::tests::Require(dirtyVelocityRows == kEntityCount, "native ECS bulk create dirty velocity row total is invalid");

    const std::array<std::size_t, 5U> createdSamples{ 0U, 17U, 512U, 1025U, kEntityCount - 1U };
    for (std::size_t index : createdSamples) {
        const kb::ecs::Entity entity = entities[index];
        kb::tests::Require(storage.IsAlive(entity), "native ECS bulk create produced a non-live entity");
        kb::tests::Require(storage.EntityArchetypeMatches(entity, movingQuery), "native ECS bulk create assigned an invalid archetype");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Position>(storage, entity, kPositionId).x, positions[index].x), "native ECS bulk create copied the wrong position row");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Velocity>(storage, entity, kVelocityId).y, velocities[index].y), "native ECS bulk create copied the wrong velocity row");
    }

    std::vector<kb::ecs::Entity> adopted;
    adopted.reserve(kEntityCount);
    for (std::size_t index = 0; index < kEntityCount; ++index) {
        adopted.push_back(kb::ecs::Entity{ 20'000'000ULL + static_cast<kb::ecs::Entity::IdType>(index) });
    }

    storage.AdoptEntities(adopted, columns);
    kb::tests::Require(storage.Stats().liveEntities == kEntityCount * 2U, "native ECS bulk adopt did not add all external entities");
    const std::array<std::size_t, 4U> adoptedSamples{ 3U, 48U, 777U, kEntityCount - 1U };
    for (std::size_t index : adoptedSamples) {
        const kb::ecs::Entity entity = adopted[index];
        kb::tests::Require(storage.IsAlive(entity), "native ECS bulk adopt produced a non-live external entity");
        kb::tests::Require(storage.EntityArchetypeMatches(entity, movingQuery), "native ECS bulk adopt assigned an invalid archetype");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Position>(storage, entity, kPositionId).y, positions[index].y), "native ECS bulk adopt copied the wrong position row");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Velocity>(storage, entity, kVelocityId).x, velocities[index].x), "native ECS bulk adopt copied the wrong velocity row");
    }

    const std::array destroyedEntities{
        entities[0],
        entities[17],
        entities[1025],
        entities[kEntityCount - 1U],
        adopted[3],
        adopted[777],
        adopted[kEntityCount - 1U],
    };
    storage.DestroyEntities(destroyedEntities);
    kb::tests::Require(storage.Stats().liveEntities == (kEntityCount * 2U) - destroyedEntities.size(), "native ECS bulk destroy did not remove the expected entity count");
    for (kb::ecs::Entity entity : destroyedEntities) {
        kb::tests::Require(!storage.IsAlive(entity), "native ECS bulk destroy kept a destroyed entity alive");
    }

    const std::array<std::size_t, 4U> survivorSamples{ 1U, 48U, 512U, 1024U };
    for (std::size_t index : survivorSamples) {
        kb::tests::Require(storage.IsAlive(entities[index]), "native ECS bulk destroy invalidated a created survivor");
        kb::tests::Require(storage.IsAlive(adopted[index]), "native ECS bulk destroy invalidated an adopted survivor");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Position>(storage, entities[index], kPositionId).x, positions[index].x), "native ECS bulk destroy corrupted a created survivor row");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Velocity>(storage, adopted[index], kVelocityId).y, velocities[index].y), "native ECS bulk destroy corrupted an adopted survivor row");
    }
}

void RunWorldNativeStorageMirrorTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk16KB;
    kb::ecs::World world{ config };

    const kb::ecs::ComponentId positionId = world.RegisterComponent<Position>("Position");
    const kb::ecs::ComponentId velocityId = world.RegisterComponent<Velocity>("Velocity");

    const kb::ecs::Entity entity = world.CreateEntity();
    kb::tests::Require(world.NativeChunkPayloadBytes() == kb::ecs::ChunkPayloadBytes(kb::ecs::ChunkSizeProfile::Chunk16KB), "World native storage did not use configured chunk payload bytes");
    kb::tests::Require(world.NativeStorageStats().liveEntities == 1, "World native storage did not mirror entity creation");
    kb::tests::Require(world.NativeStorage().IsAlive(entity), "World native storage did not adopt the runtime entity handle");

    const Position position{ .x = 11.0F, .y = 12.0F };
    world.Set(entity, position);
    kb::tests::Require(world.NativeStorage().HasComponent(entity, positionId), "World native storage did not mirror component set as add");
    kb::tests::Require(kb::tests::NearlyEqual(Component<Position>(world.NativeStorage(), entity, positionId).x, 11.0F), "World native storage mirrored invalid component data");

    const Position updatedPosition{ .x = 21.0F, .y = 22.0F };
    world.Set(entity, updatedPosition);
    kb::tests::Require(kb::tests::NearlyEqual(Component<Position>(world.NativeStorage(), entity, positionId).x, 21.0F), "World native storage did not mirror component overwrite");

    const Velocity velocity{ .x = 1.0F, .y = 2.0F };
    world.Set(entity, velocity);
    const std::array movingQuery{ positionId, velocityId };
    kb::tests::Require(world.NativeStorage().EntityArchetypeMatches(entity, movingQuery), "World native storage did not mirror add-component archetype migration");

    world.Remove<Velocity>(entity);
    kb::tests::Require(!world.NativeStorage().HasComponent(entity, velocityId), "World native storage did not mirror component removal");
    kb::tests::Require(world.NativeStorage().HasComponent(entity, positionId), "World native storage lost retained component during removal");

    world.DestroyEntity(entity);
    kb::tests::Require(!world.NativeStorage().IsAlive(entity), "World native storage did not mirror entity destruction");
    kb::tests::Require(world.NativeStorageStats().liveEntities == 0, "World native storage stats did not mirror entity destruction");
}

void RunWorldDirectBulkCreateTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk16KB;
    kb::ecs::World world{ config };

    constexpr std::size_t kEntityCount = 48U;
    std::vector<Position> positions;
    std::vector<Velocity> velocities;
    positions.reserve(kEntityCount);
    velocities.reserve(kEntityCount);
    for (std::size_t index = 0; index < kEntityCount; ++index) {
        positions.push_back(Position{ .x = static_cast<float>(index), .y = static_cast<float>(index + 10U) });
        velocities.push_back(Velocity{ .x = static_cast<float>(index + 20U), .y = static_cast<float>(index + 30U) });
    }

    const std::array componentViews{
        kb::ecs::World::MakeBulkComponentView<Position>(std::span<const Position>{ positions }),
        kb::ecs::World::MakeBulkComponentView<Velocity>(std::span<const Velocity>{ velocities }),
    };
    const std::vector<kb::ecs::Entity> entities = world.CreateEntities(kEntityCount, std::span<const kb::ecs::World::BulkComponentView>{ componentViews });
    kb::tests::Require(entities.size() == kEntityCount, "World direct bulk create returned an unexpected entity count");
    RequireStorageStatsConsistent(world.NativeStorageStats(), kEntityCount, "World direct bulk create storage stats are inconsistent");

    const kb::ecs::ComponentId positionId = world.Component<Position>();
    const kb::ecs::ComponentId velocityId = world.Component<Velocity>();
    const std::array archetype{ positionId, velocityId };
    for (std::size_t index = 0; index < entities.size(); ++index) {
        const kb::ecs::Entity entity = entities[index];
        kb::tests::Require(world.IsAlive(entity), "World direct bulk create did not create a live entity");
        kb::tests::Require(world.NativeStorage().EntityArchetypeMatches(entity, std::span<const kb::ecs::ComponentId>{ archetype }), "World direct bulk create assigned an invalid archetype");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Position>(world.NativeStorage(), entity, positionId).x, positions[index].x), "World direct bulk create wrote the wrong position row");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Velocity>(world.NativeStorage(), entity, velocityId).y, velocities[index].y), "World direct bulk create wrote the wrong velocity row");
    }
}

void RunWorldDirectBulkCreateIntoReuseTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk16KB;
    kb::ecs::World world{ config };

    constexpr std::size_t kEntityCount = 32U;
    std::vector<Position> positions;
    std::vector<Velocity> velocities;
    positions.reserve(kEntityCount);
    velocities.reserve(kEntityCount);
    for (std::size_t index = 0; index < kEntityCount; ++index) {
        positions.push_back(Position{ .x = static_cast<float>(index + 3U), .y = static_cast<float>(index + 4U) });
        velocities.push_back(Velocity{ .x = static_cast<float>(index + 5U), .y = static_cast<float>(index + 6U) });
    }

    const std::array componentViews{
        kb::ecs::World::MakeBulkComponentView<Position>(std::span<const Position>{ positions }),
        kb::ecs::World::MakeBulkComponentView<Velocity>(std::span<const Velocity>{ velocities }),
    };
    std::vector<kb::ecs::Entity> entities;
    entities.reserve(128U);
    const std::size_t preservedCapacity = entities.capacity();
    entities.push_back(kb::ecs::Entity{ 123U });

    world.CreateEntitiesNativeOnlyInto(entities, kEntityCount, std::span<const kb::ecs::World::BulkComponentView>{ componentViews });
    kb::tests::Require(entities.size() == kEntityCount, "World direct bulk create-into returned an unexpected entity count");
    kb::tests::Require(entities.capacity() == preservedCapacity, "World direct bulk create-into did not reuse the caller-provided buffer");
    RequireStorageStatsConsistent(world.NativeStorageStats(), kEntityCount, "World direct bulk create-into storage stats are inconsistent");

    const kb::ecs::ComponentId positionId = world.Component<Position>();
    const kb::ecs::ComponentId velocityId = world.Component<Velocity>();
    const std::array archetype{ positionId, velocityId };
    for (std::size_t index = 0; index < entities.size(); ++index) {
        const kb::ecs::Entity entity = entities[index];
        kb::tests::Require(world.IsAlive(entity), "World direct bulk create-into did not create a live entity");
        kb::tests::Require(world.NativeStorage().EntityArchetypeMatches(entity, std::span<const kb::ecs::ComponentId>{ archetype }), "World direct bulk create-into assigned an invalid archetype");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Position>(world.NativeStorage(), entity, positionId).x, positions[index].x), "World direct bulk create-into wrote the wrong position row");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Velocity>(world.NativeStorage(), entity, velocityId).y, velocities[index].y), "World direct bulk create-into wrote the wrong velocity row");
    }
}

void RunWorldDirectBulkBroadcastCreateTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk16KB;
    kb::ecs::World world{ config };

    constexpr std::size_t kEntityCount = 64U;
    const Position position{ .x = 7.0F, .y = 9.0F };
    const Velocity velocity{ .x = 2.0F, .y = 4.0F };
    const std::array componentViews{
        kb::ecs::World::MakeBulkComponentBroadcastView<Position>(position),
        kb::ecs::World::MakeBulkComponentBroadcastView<Velocity>(velocity),
    };
    std::array<kb::ecs::World::BulkComponentView, componentViews.size()> broadcastViews = componentViews;
    for (kb::ecs::World::BulkComponentView& view : broadcastViews) {
        view.componentCount = kEntityCount;
    }

    const std::vector<kb::ecs::Entity> entities = world.CreateEntities(kEntityCount, std::span<const kb::ecs::World::BulkComponentView>{ broadcastViews });
    kb::tests::Require(entities.size() == kEntityCount, "World direct bulk broadcast create returned an unexpected entity count");
    RequireStorageStatsConsistent(world.NativeStorageStats(), kEntityCount, "World direct bulk broadcast create storage stats are inconsistent");

    const kb::ecs::ComponentId positionId = world.Component<Position>();
    const kb::ecs::ComponentId velocityId = world.Component<Velocity>();
    const std::array archetype{ positionId, velocityId };
    for (kb::ecs::Entity entity : entities) {
        kb::tests::Require(world.NativeStorage().EntityArchetypeMatches(entity, std::span<const kb::ecs::ComponentId>{ archetype }), "World direct bulk broadcast create assigned an invalid archetype");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Position>(world.NativeStorage(), entity, positionId).x, position.x), "World direct bulk broadcast create wrote the wrong position row");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Velocity>(world.NativeStorage(), entity, velocityId).y, velocity.y), "World direct bulk broadcast create wrote the wrong velocity row");
        kb::tests::Require(kb::tests::NearlyEqual(BackendComponent<Position>(world, entity, positionId).x, position.x), "World direct bulk broadcast create mirrored the wrong backend position row");
        kb::tests::Require(kb::tests::NearlyEqual(BackendComponent<Velocity>(world, entity, velocityId).y, velocity.y), "World direct bulk broadcast create mirrored the wrong backend velocity row");
    }
}

void RunWorldDirectBulkPatternCreateTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk4KB;
    kb::ecs::World world{ config };

    constexpr std::size_t kEntityCount = 64U;
    const std::array positions{
        Position{ .x = 1.0F, .y = 10.0F },
        Position{ .x = 2.0F, .y = 20.0F },
    };
    const std::array velocities{
        Velocity{ .x = 3.0F, .y = 30.0F },
        Velocity{ .x = 4.0F, .y = 40.0F },
    };
    std::array componentViews{
        kb::ecs::World::MakeBulkComponentView<Position>(std::span<const Position>{ positions }),
        kb::ecs::World::MakeBulkComponentView<Velocity>(std::span<const Velocity>{ velocities }),
    };
    for (kb::ecs::World::BulkComponentView& view : componentViews) {
        view.componentCount = kEntityCount;
        view.sourceCount = positions.size();
    }

    const std::vector<kb::ecs::Entity> entities = world.CreateEntities(kEntityCount, std::span<const kb::ecs::World::BulkComponentView>{ componentViews });
    kb::tests::Require(entities.size() == kEntityCount, "World direct bulk pattern create returned an unexpected entity count");
    RequireStorageStatsConsistent(world.NativeStorageStats(), kEntityCount, "World direct bulk pattern create storage stats are inconsistent");

    const kb::ecs::ComponentId positionId = world.Component<Position>();
    const kb::ecs::ComponentId velocityId = world.Component<Velocity>();
    const std::array archetype{ positionId, velocityId };
    for (std::size_t index = 0; index < entities.size(); ++index) {
        const kb::ecs::Entity entity = entities[index];
        const std::size_t sourceIndex = index % positions.size();
        kb::tests::Require(world.NativeStorage().EntityArchetypeMatches(entity, std::span<const kb::ecs::ComponentId>{ archetype }), "World direct bulk pattern create assigned an invalid archetype");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Position>(world.NativeStorage(), entity, positionId).x, positions[sourceIndex].x), "World direct bulk pattern create wrote the wrong position row");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Velocity>(world.NativeStorage(), entity, velocityId).y, velocities[sourceIndex].y), "World direct bulk pattern create wrote the wrong velocity row");
        kb::tests::Require(kb::tests::NearlyEqual(BackendComponent<Position>(world, entity, positionId).x, positions[sourceIndex].x), "World direct bulk pattern create mirrored the wrong backend position row");
        kb::tests::Require(kb::tests::NearlyEqual(BackendComponent<Velocity>(world, entity, velocityId).y, velocities[sourceIndex].y), "World direct bulk pattern create mirrored the wrong backend velocity row");
    }
}

void RunWorldNativeStorageMaintenanceTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk4KB;
    kb::ecs::World world{ config };

    constexpr std::size_t kEntityCount = 1537U;
    std::vector<Position> positions;
    positions.reserve(kEntityCount);
    for (std::size_t index = 0; index < kEntityCount; ++index) {
        positions.push_back(Position{ .x = static_cast<float>(index), .y = static_cast<float>(index + 1U) });
    }
    const std::array componentViews{
        kb::ecs::World::MakeBulkComponentView<Position>(std::span<const Position>{ positions }),
    };

    const std::vector<kb::ecs::Entity> entities = world.CreateEntitiesNativeOnly(kEntityCount, std::span<const kb::ecs::World::BulkComponentView>{ componentViews });
    kb::tests::Require(world.NativeStorageStats().chunks >= 4U, "World native maintenance setup did not allocate multiple chunks");
    for (kb::ecs::Entity entity : entities) {
        world.DestroyEntity(entity);
    }

    const kb::ecs::NativeEcsStorageStats beforeMaintenance = world.NativeStorageStats();
    kb::tests::Require(beforeMaintenance.chunkPoolFree >= 4U, "World native maintenance setup did not retain free chunks");
    const kb::ecs::NativeEcsMaintenanceStats maintenance = world.MaintainNativeStorage(kb::ecs::NativeEcsMaintenanceBudget{
        .maxFreeChunksToKeep = 1U,
        .maxChunksToRelease = 2U,
    });
    const kb::ecs::WorldTelemetrySnapshot telemetry = world.TelemetrySnapshot();

    kb::tests::Require(maintenance.chunksReleasedToSystem == 2U, "World native maintenance ignored release budget");
    kb::tests::Require(world.NativeStorageStats().chunkPoolFree == beforeMaintenance.chunkPoolFree - 2U, "World native maintenance did not trim the expected free chunks");
    kb::tests::Require(telemetry.chunkPoolTrimCount == 2U, "World telemetry did not expose native maintenance trim count");
}

void RunWorldBulkStorageStatsConsistencyTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk16KB;
    kb::ecs::World world{ config };

    std::vector<Position> positions;
    std::vector<Velocity> velocities;
    positions.reserve(64U);
    velocities.reserve(64U);
    for (std::size_t index = 0; index < 64U; ++index) {
        positions.push_back(Position{ .x = static_cast<float>(index), .y = static_cast<float>(index + 1U) });
        velocities.push_back(Velocity{ .x = static_cast<float>(index * 2U), .y = static_cast<float>(index * 3U) });
    }

    kb::ecs::CommandBuffer createBuffer{ 1 };
    const std::vector<kb::ecs::CommandEntity> commandEntities = createBuffer.Worker(0).CreateEntities(std::span<const Position>{ positions }, std::span<const Velocity>{ velocities });
    const kb::ecs::CommandBufferPlaybackResult createResult = createBuffer.Playback(world);
    std::vector<kb::ecs::Entity> entities;
    entities.reserve(commandEntities.size());
    for (kb::ecs::CommandEntity commandEntity : commandEntities) {
        entities.push_back(createResult.Resolve(commandEntity));
    }
    kb::tests::Require(entities.size() == positions.size(), "World bulk storage stats setup did not create all entities");
    RequireStorageStatsConsistent(world.NativeStorageStats(), entities.size(), "World bulk create storage stats are inconsistent");

    std::vector<kb::ecs::Entity> evenEntities;
    std::vector<Mass> masses;
    evenEntities.reserve(entities.size() / 2U);
    masses.reserve(entities.size() / 2U);
    for (std::size_t index = 0; index < entities.size(); index += 2U) {
        evenEntities.push_back(entities[index]);
        masses.push_back(Mass{ .value = 10.0F });
    }
    kb::ecs::CommandBuffer addBuffer{ 1 };
    addBuffer.Worker(0).Set(std::span<const kb::ecs::Entity>{ evenEntities }, std::span<const Mass>{ masses });
    static_cast<void>(addBuffer.Playback(world));
    RequireStorageStatsConsistent(world.NativeStorageStats(), entities.size(), "World bulk add/migration storage stats are inconsistent");

    std::vector<kb::ecs::Entity> oddEntities;
    oddEntities.reserve(entities.size() / 2U);
    for (std::size_t index = 1U; index < entities.size(); index += 2U) {
        oddEntities.push_back(entities[index]);
    }
    kb::ecs::CommandBuffer removeBuffer{ 1 };
    removeBuffer.Worker(0).Remove<Velocity>(std::span<const kb::ecs::Entity>{ oddEntities });
    static_cast<void>(removeBuffer.Playback(world));
    RequireStorageStatsConsistent(world.NativeStorageStats(), entities.size(), "World bulk remove/migration storage stats are inconsistent");

    kb::ecs::CommandBuffer destroyBuffer{ 1 };
    for (std::size_t index = 0; index < entities.size(); index += 3U) {
        destroyBuffer.Worker(0).DestroyEntity(entities[index]);
    }
    static_cast<void>(destroyBuffer.Playback(world));
    const std::size_t destroyedCount = (entities.size() + 2U) / 3U;
    RequireStorageStatsConsistent(world.NativeStorageStats(), entities.size() - destroyedCount, "World bulk destroy storage stats are inconsistent");
}

void RunWorldBulkMappingIntegrityTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk16KB;
    kb::ecs::World world{ config };

    const kb::ecs::ComponentId positionId = world.RegisterComponent<Position>("Position");
    const kb::ecs::ComponentId velocityId = world.RegisterComponent<Velocity>("Velocity");
    const kb::ecs::ComponentId massId = world.RegisterComponent<Mass>("Mass");

    constexpr std::size_t kEntityCount = 96U;
    std::vector<Position> positions;
    std::vector<Velocity> velocities;
    positions.reserve(kEntityCount);
    velocities.reserve(kEntityCount);
    for (std::size_t index = 0; index < kEntityCount; ++index) {
        positions.push_back(Position{ .x = static_cast<float>(index), .y = static_cast<float>(index + 100U) });
        velocities.push_back(Velocity{ .x = static_cast<float>(index + 200U), .y = static_cast<float>(index + 300U) });
    }

    kb::ecs::CommandBuffer createBuffer{ 1 };
    const std::vector<kb::ecs::CommandEntity> commandEntities = createBuffer.Worker(0).CreateEntities(std::span<const Position>{ positions }, std::span<const Velocity>{ velocities });
    const kb::ecs::CommandBufferPlaybackResult createResult = createBuffer.Playback(world);

    std::vector<kb::ecs::Entity> entities;
    entities.reserve(commandEntities.size());
    for (const kb::ecs::CommandEntity commandEntity : commandEntities) {
        entities.push_back(createResult.Resolve(commandEntity));
    }
    kb::tests::Require(entities.size() == kEntityCount, "World bulk mapping setup did not create all entities");

    const std::array movingQuery{ positionId, velocityId };
    for (std::size_t index = 0; index < entities.size(); ++index) {
        const kb::ecs::Entity entity = entities[index];
        kb::tests::Require(world.NativeStorage().IsAlive(entity), "World bulk create mapping did not adopt a live native entity");
        kb::tests::Require(world.NativeStorage().EntityArchetypeMatches(entity, movingQuery), "World bulk create mapping assigned an invalid archetype");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Position>(world.NativeStorage(), entity, positionId).x, positions[index].x), "World bulk create mapping read the wrong position row");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Velocity>(world.NativeStorage(), entity, velocityId).y, velocities[index].y), "World bulk create mapping read the wrong velocity row");
    }

    std::vector<bool> destroyed(entities.size(), false);
    kb::ecs::CommandBuffer destroyBuffer{ 1 };
    for (std::size_t index = 0; index < entities.size(); index += 5U) {
        destroyed[index] = true;
        destroyBuffer.Worker(0).DestroyEntity(entities[index]);
    }
    static_cast<void>(destroyBuffer.Playback(world));

    for (std::size_t index = 0; index < entities.size(); ++index) {
        const kb::ecs::Entity entity = entities[index];
        if (destroyed[index]) {
            kb::tests::Require(!world.NativeStorage().IsAlive(entity), "World bulk destroy mapping kept a destroyed native entity alive");
            continue;
        }

        kb::tests::Require(world.NativeStorage().IsAlive(entity), "World bulk destroy mapping invalidated an unrelated entity");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Position>(world.NativeStorage(), entity, positionId).x, positions[index].x), "World bulk destroy mapping corrupted a moved position row");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Velocity>(world.NativeStorage(), entity, velocityId).x, velocities[index].x), "World bulk destroy mapping corrupted a moved velocity row");
    }

    std::vector<kb::ecs::Entity> addMassEntities;
    std::vector<Mass> masses;
    addMassEntities.reserve(entities.size());
    masses.reserve(entities.size());
    for (std::size_t index = 0; index < entities.size(); ++index) {
        if (!destroyed[index] && index % 2U == 0U) {
            addMassEntities.push_back(entities[index]);
            masses.push_back(Mass{ .value = static_cast<float>(index + 400U) });
        }
    }

    kb::ecs::CommandBuffer addBuffer{ 1 };
    addBuffer.Worker(0).Set(std::span<const kb::ecs::Entity>{ addMassEntities }, std::span<const Mass>{ masses });
    static_cast<void>(addBuffer.Playback(world));

    const std::array weightedMovingQuery{ positionId, velocityId, massId };
    for (std::size_t index = 0; index < addMassEntities.size(); ++index) {
        const kb::ecs::Entity entity = addMassEntities[index];
        kb::tests::Require(world.NativeStorage().EntityArchetypeMatches(entity, weightedMovingQuery), "World bulk add mapping assigned an invalid target archetype");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Mass>(world.NativeStorage(), entity, massId).value, masses[index].value), "World bulk add mapping read the wrong mass row");
    }

    std::vector<kb::ecs::Entity> removeVelocityEntities;
    removeVelocityEntities.reserve(entities.size());
    for (std::size_t index = 0; index < entities.size(); ++index) {
        if (!destroyed[index] && index % 3U == 1U) {
            removeVelocityEntities.push_back(entities[index]);
        }
    }

    kb::ecs::CommandBuffer removeBuffer{ 1 };
    removeBuffer.Worker(0).Remove<Velocity>(std::span<const kb::ecs::Entity>{ removeVelocityEntities });
    static_cast<void>(removeBuffer.Playback(world));

    const std::array positionOnlyQuery{ positionId };
    for (const kb::ecs::Entity entity : removeVelocityEntities) {
        kb::tests::Require(world.NativeStorage().IsAlive(entity), "World bulk remove mapping invalidated a migrated entity");
        kb::tests::Require(!world.NativeStorage().HasComponent(entity, velocityId), "World bulk remove mapping retained removed velocity");
        kb::tests::Require(world.NativeStorage().EntityArchetypeMatches(entity, positionOnlyQuery), "World bulk remove mapping lost retained position archetype membership");
    }
}

void RunWorldBulkVersioningTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk16KB;
    kb::ecs::World world{ config };

    const kb::ecs::ComponentId positionId = world.RegisterComponent<Position>("Position");
    const kb::ecs::ComponentId velocityId = world.RegisterComponent<Velocity>("Velocity");
    const kb::ecs::ComponentId massId = world.RegisterComponent<Mass>("Mass");

    std::vector<Position> positions;
    std::vector<Velocity> velocities;
    positions.reserve(32U);
    velocities.reserve(32U);
    for (std::size_t index = 0; index < 32U; ++index) {
        positions.push_back(Position{ .x = static_cast<float>(index), .y = static_cast<float>(index + 1U) });
        velocities.push_back(Velocity{ .x = static_cast<float>(index + 2U), .y = static_cast<float>(index + 3U) });
    }

    kb::ecs::CommandBuffer createBuffer{ 1 };
    const std::vector<kb::ecs::CommandEntity> commandEntities = createBuffer.Worker(0).CreateEntities(std::span<const Position>{ positions }, std::span<const Velocity>{ velocities });
    const kb::ecs::CommandBufferPlaybackResult createResult = createBuffer.Playback(world);
    std::vector<kb::ecs::Entity> entities;
    entities.reserve(commandEntities.size());
    for (kb::ecs::CommandEntity commandEntity : commandEntities) {
        entities.push_back(createResult.Resolve(commandEntity));
    }
    kb::tests::Require(entities.size() == positions.size(), "World bulk versioning setup did not create all entities");

    const kb::ecs::Entity sample = entities.front();
    const std::uint64_t positionVersionBeforeWrite = world.NativeStorage().ComponentVersion(sample, positionId);
    const std::uint64_t velocityVersionBeforeWrite = world.NativeStorage().ComponentVersion(sample, velocityId);

    std::vector<Position> updatedPositions;
    updatedPositions.reserve(entities.size());
    for (std::size_t index = 0; index < entities.size(); ++index) {
        updatedPositions.push_back(Position{ .x = static_cast<float>(index + 100U), .y = static_cast<float>(index + 200U) });
    }
    kb::ecs::CommandBuffer writeBuffer{ 1 };
    writeBuffer.Worker(0).Set(std::span<const kb::ecs::Entity>{ entities }, std::span<const Position>{ updatedPositions });
    static_cast<void>(writeBuffer.Playback(world));

    kb::tests::Require(
        world.NativeStorage().ComponentVersion(sample, positionId) > positionVersionBeforeWrite,
        "World bulk component write did not advance the written component version");
    kb::tests::Require(
        world.NativeStorage().ComponentVersion(sample, velocityId) == velocityVersionBeforeWrite,
        "World bulk component write advanced an unchanged component version");

    std::vector<kb::ecs::Entity> migratedEntities;
    std::vector<Mass> masses;
    migratedEntities.reserve(entities.size() / 2U);
    masses.reserve(entities.size() / 2U);
    for (std::size_t index = 0; index < entities.size(); index += 2U) {
        migratedEntities.push_back(entities[index]);
        masses.push_back(Mass{ .value = static_cast<float>(index + 1U) });
    }

    kb::ecs::CommandBuffer addBuffer{ 1 };
    addBuffer.Worker(0).Set(std::span<const kb::ecs::Entity>{ migratedEntities }, std::span<const Mass>{ masses });
    static_cast<void>(addBuffer.Playback(world));

    const kb::ecs::Entity migratedSample = migratedEntities.front();
    kb::tests::Require(world.NativeStorage().HasComponent(migratedSample, massId), "World bulk add migration did not add the new component");
    kb::tests::Require(world.NativeStorage().ComponentVersion(migratedSample, positionId) > 0U, "World bulk add migration lost retained component versioning");
    kb::tests::Require(world.NativeStorage().ComponentVersion(migratedSample, velocityId) > 0U, "World bulk add migration lost second retained component versioning");
    kb::tests::Require(world.NativeStorage().ComponentVersion(migratedSample, massId) > 0U, "World bulk add migration did not initialize added component versioning");

    kb::ecs::CommandBuffer removeBuffer{ 1 };
    removeBuffer.Worker(0).Remove<Velocity>(std::span<const kb::ecs::Entity>{ migratedEntities });
    static_cast<void>(removeBuffer.Playback(world));

    kb::tests::Require(!world.NativeStorage().HasComponent(migratedSample, velocityId), "World bulk remove migration retained removed component");
    kb::tests::Require(world.NativeStorage().HasComponent(migratedSample, positionId), "World bulk remove migration lost retained position component");
    kb::tests::Require(world.NativeStorage().HasComponent(migratedSample, massId), "World bulk remove migration lost retained mass component");
    kb::tests::Require(world.NativeStorage().ComponentVersion(migratedSample, positionId) > 0U, "World bulk remove migration lost retained component versioning");
    kb::tests::Require(world.NativeStorage().ComponentVersion(migratedSample, massId) > 0U, "World bulk remove migration lost retained mass versioning");
}

void RunNativeComponentAlignmentTest() {
    kb::ecs::NativeArchetypeStorage storage;

    WideAlignedComponent first{};
    first.values[0] = 1.0F;
    WideAlignedComponent second{};
    second.values[0] = 2.0F;
    const kb::ecs::NativeComponentType wideType = ComponentType<WideAlignedComponent>(kWideAlignedId);
    const std::array firstComponents{ kb::ecs::NativeComponentValue{ .type = wideType, .data = &first } };
    const std::array secondComponents{ kb::ecs::NativeComponentValue{ .type = wideType, .data = &second } };

    const kb::ecs::Entity firstEntity = storage.CreateEntity(firstComponents);
    const kb::ecs::Entity secondEntity = storage.CreateEntity(secondComponents);
    kb::tests::Require(storage.IsAlive(secondEntity), "native ECS aligned component setup did not create the second entity");

    kb::tests::Require(IsAligned(storage.ComponentData(firstEntity, kWideAlignedId), alignof(WideAlignedComponent)), "native ECS component data did not preserve declared alignment");

    std::vector<kb::ecs::QueryTableDispatchRecord> records;
    const std::array queryIds{ kWideAlignedId };
    storage.CollectQueryRecords(queryIds, {}, {}, records);
    kb::tests::Require(records.size() == 1U && records.front().entityCount == 2U, "native ECS aligned component query did not return the expected chunk");
    kb::tests::Require(IsAligned(records.front().fieldComponents[0], alignof(WideAlignedComponent)), "native ECS readonly query returned an unaligned component column");

    std::vector<kb::ecs::MutableQueryTableDispatchRecord> mutableRecords;
    storage.CollectMutableQueryRecords(queryIds, {}, {}, mutableRecords);
    kb::tests::Require(mutableRecords.size() == 1U && mutableRecords.front().entityCount == 2U, "native ECS mutable aligned component query did not return the expected chunk");
    kb::tests::Require(IsAligned(mutableRecords.front().fieldComponents[0], alignof(WideAlignedComponent)), "native ECS mutable query returned an unaligned component column");
}

void RunNativeComponentDirtyRangeTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk4KB;
    kb::ecs::NativeArchetypeStorage storage{ config };

    std::vector<Position> positions{
        Position{ .x = 1.0F, .y = 1.0F },
        Position{ .x = 2.0F, .y = 2.0F },
        Position{ .x = 3.0F, .y = 3.0F },
        Position{ .x = 4.0F, .y = 4.0F },
        Position{ .x = 5.0F, .y = 5.0F },
        Position{ .x = 6.0F, .y = 6.0F },
        Position{ .x = 7.0F, .y = 7.0F },
        Position{ .x = 8.0F, .y = 8.0F },
    };
    const kb::ecs::NativeBulkComponentColumn positionColumn{
        .type = ComponentType<Position>(kPositionId),
        .data = positions.data(),
        .stride = sizeof(Position),
        .sourceCount = positions.size(),
    };
    const std::vector<kb::ecs::Entity> entities = storage.CreateEntities(positions.size(), std::span<const kb::ecs::NativeBulkComponentColumn>{ &positionColumn, 1U });

    const std::array queryIds{ kPositionId };
    std::vector<kb::ecs::QueryTableDispatchRecord> records;
    storage.CollectQueryRecords(queryIds, {}, {}, records);
    kb::tests::Require(records.size() == 1U, "native ECS dirty range setup did not produce one chunk");

    const kb::ecs::QueryTableDispatchRecord& record = records.front();
    kb::tests::Require(record.componentDirtyCounts[0] == positions.size(), "native ECS query did not expose initial dirty row count");
    kb::tests::Require(
        storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, kPositionId) == positions.size(),
        "native ECS component dirty count did not include bulk-created rows");

    std::vector<kb::ecs::NativeComponentDirtyRange> ranges;
    const std::size_t initialRanges = storage.CollectComponentDirtyRanges(record.nativeArchetypeIndex, record.nativeChunkIndex, kPositionId, 4U, ranges);
    kb::tests::Require(initialRanges == 2U, "native ECS component dirty range summary did not split by requested range size");
    kb::tests::Require(ranges[0].begin == 0U && ranges[0].count == 4U && ranges[0].dirtyCount == 4U, "native ECS first dirty range is invalid");
    kb::tests::Require(ranges[1].begin == 4U && ranges[1].count == 4U && ranges[1].dirtyCount == 4U, "native ECS second dirty range is invalid");

    storage.ClearComponentDirtyRows(record.nativeArchetypeIndex, record.nativeChunkIndex, kPositionId);
    kb::tests::Require(storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, kPositionId) == 0U, "native ECS dirty clear did not reset the component row bits");
    ranges.clear();
    kb::tests::Require(
        storage.CollectComponentDirtyRanges(record.nativeArchetypeIndex, record.nativeChunkIndex, kPositionId, 4U, ranges) == 0U,
        "native ECS clean component produced dirty range summaries");

    Position updated{ .x = 30.0F, .y = 40.0F };
    storage.SetComponent(entities[3], kPositionId, &updated, sizeof(updated));
    kb::tests::Require(storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, kPositionId) == 1U, "native ECS point set did not dirty exactly one row");

    ranges.clear();
    kb::tests::Require(
        storage.CollectComponentDirtyRanges(record.nativeArchetypeIndex, record.nativeChunkIndex, kPositionId, 2U, ranges) == 1U,
        "native ECS point set produced an invalid dirty range count");
    kb::tests::Require(ranges.front().begin == 2U && ranges.front().count == 2U && ranges.front().dirtyCount == 1U, "native ECS point set dirty range is invalid");

    storage.ClearComponentDirtyRows(record.nativeArchetypeIndex, record.nativeChunkIndex, kPositionId, 2U, 2U);
    kb::tests::Require(storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, kPositionId) == 0U, "native ECS partial dirty clear left stale row bits");

    storage.MarkArchetypeChunkComponentsModified(record.nativeArchetypeIndex, record.nativeChunkIndex, 5U, 2U, queryIds);
    kb::tests::Require(storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, kPositionId) == 2U, "native ECS chunk range mark did not dirty the requested rows");

    std::vector<kb::ecs::MutableQueryTableDispatchRecord> mutableRecords;
    storage.CollectMutableQueryRecords(queryIds, {}, {}, mutableRecords);
    kb::tests::Require(mutableRecords.size() == 1U && mutableRecords.front().componentDirtyCounts[0] == 2U, "native ECS mutable query did not expose component dirty counts");
}

void RunNativeComponentAlignmentValidationTest() {
    kb::ecs::NativeArchetypeStorage storage;
    std::uint32_t value = 7U;
    const std::array invalidComponents{
        kb::ecs::NativeComponentValue{
            .type = kb::ecs::NativeComponentType{ .id = kWideAlignedId, .size = sizeof(value), .alignment = 8U },
            .data = &value,
        },
    };

    bool rejected = false;
    try {
        const kb::ecs::Entity invalidEntity = storage.CreateEntity(invalidComponents);
        static_cast<void>(invalidEntity);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }

    kb::tests::Require(rejected, "native ECS storage accepted a component type whose size cannot preserve row alignment");
}

void RunBulkDestroyDuplicateValidationTest() {
    kb::ecs::NativeArchetypeStorage storage;
    std::array<Position, 4U> positions{
        Position{ .x = 1.0F, .y = 2.0F },
        Position{ .x = 3.0F, .y = 4.0F },
        Position{ .x = 5.0F, .y = 6.0F },
        Position{ .x = 7.0F, .y = 8.0F },
    };
    const std::array columns{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Position>(kPositionId),
            .data = positions.data(),
            .stride = sizeof(Position),
        },
    };
    const std::vector<kb::ecs::Entity> entities = storage.CreateEntities(positions.size(), columns);

    bool rejectedDuplicate = false;
    const std::array invalidBatch{
        entities[1],
        entities[2],
        entities[1],
    };
    try {
        storage.DestroyEntities(invalidBatch);
    } catch (const std::invalid_argument&) {
        rejectedDuplicate = true;
    }

    kb::tests::Require(rejectedDuplicate, "native ECS bulk destroy accepted duplicate entities");
    kb::tests::Require(storage.Stats().liveEntities == entities.size(), "native ECS bulk destroy duplicate validation partially mutated storage");
    for (kb::ecs::Entity entity : entities) {
        kb::tests::Require(storage.IsAlive(entity), "native ECS bulk destroy duplicate validation destroyed an entity");
    }
}

void RunBulkDestroyAllFastPathTest() {
    kb::ecs::NativeArchetypeStorage storage(kb::ecs::WorldConfig{
        .reserveEntities = 16,
    });
    std::array<Position, 6U> positions{
        Position{ .x = 1.0F, .y = 2.0F },
        Position{ .x = 3.0F, .y = 4.0F },
        Position{ .x = 5.0F, .y = 6.0F },
        Position{ .x = 7.0F, .y = 8.0F },
        Position{ .x = 9.0F, .y = 10.0F },
        Position{ .x = 11.0F, .y = 12.0F },
    };
    std::array<Velocity, 6U> velocities{
        Velocity{ .x = 0.5F, .y = 1.5F },
        Velocity{ .x = 2.5F, .y = 3.5F },
        Velocity{ .x = 4.5F, .y = 5.5F },
        Velocity{ .x = 6.5F, .y = 7.5F },
        Velocity{ .x = 8.5F, .y = 9.5F },
        Velocity{ .x = 10.5F, .y = 11.5F },
    };
    const std::array columns{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Position>(kPositionId),
            .data = positions.data(),
            .stride = sizeof(Position),
        },
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Velocity>(kVelocityId),
            .data = velocities.data(),
            .stride = sizeof(Velocity),
        },
    };

    const std::vector<kb::ecs::Entity> generated = storage.CreateEntities(positions.size(), columns);
    const std::array external{
        kb::ecs::Entity{ 45'000'000ULL },
        kb::ecs::Entity{ 45'000'001ULL },
        kb::ecs::Entity{ 45'000'002ULL },
        kb::ecs::Entity{ 45'000'003ULL },
        kb::ecs::Entity{ 45'000'004ULL },
        kb::ecs::Entity{ 45'000'005ULL },
    };
    storage.AdoptEntities(external, columns);
    const kb::ecs::NativeEcsStorageStats beforeDestroy = storage.Stats();
    kb::tests::Require(beforeDestroy.liveEntities == generated.size() + external.size(), "native ECS destroy-all setup did not create every entity");
    kb::tests::Require(beforeDestroy.chunkPoolInUse > 0U, "native ECS destroy-all setup did not allocate chunks");

    std::vector<kb::ecs::Entity> allEntities;
    allEntities.reserve(generated.size() + external.size());
    allEntities.insert(allEntities.end(), generated.begin(), generated.end());
    allEntities.insert(allEntities.end(), external.begin(), external.end());
    storage.DestroyEntities(allEntities);

    const kb::ecs::NativeEcsStorageStats afterDestroy = storage.Stats();
    RequireStorageStatsConsistent(afterDestroy, 0U, "native ECS destroy-all fast path left inconsistent stats");
    kb::tests::Require(afterDestroy.chunks == 0U, "native ECS destroy-all fast path left live chunks");
    kb::tests::Require(afterDestroy.chunkPoolInUse == 0U, "native ECS destroy-all fast path did not release chunk pool blocks");
    kb::tests::Require(afterDestroy.chunkPoolFree == beforeDestroy.chunkPoolInUse, "native ECS destroy-all fast path did not retain released chunks");
    for (kb::ecs::Entity entity : allEntities) {
        kb::tests::Require(!storage.IsAlive(entity), "native ECS destroy-all fast path left an entity alive");
    }
    for (kb::ecs::Entity entity : external) {
        kb::tests::Require(!storage.ResolveAliveEntity(entity.Id()).IsValid(), "native ECS destroy-all fast path left an external lookup alive");
    }
    std::vector<kb::ecs::Entity> recreated;
    storage.CreateEntitiesInto(recreated, generated.size(), columns);
    kb::tests::Require(recreated.size() == generated.size(), "native ECS destroy-all fast path broke bulk generated id reuse");
    for (std::size_t index = 0U; index < recreated.size(); ++index) {
        const kb::ecs::Entity entity = recreated[index];
        kb::tests::Require(storage.IsAlive(entity), "native ECS destroy-all fast path bulk recreate produced a dead entity");
        kb::tests::Require(entity != generated[index], "native ECS destroy-all fast path did not advance generated entity generation");
    }
    for (std::size_t index = 1U; index < recreated.size(); ++index) {
        kb::tests::Require(recreated[index - 1U].Id() < recreated[index].Id(), "native ECS destroy-all fast path bulk recreate returned unsorted generated ids");
    }

    storage.Clear();
    const kb::ecs::NativeEcsStorageStats afterClear = storage.Stats();
    RequireStorageStatsConsistent(afterClear, 0U, "native ECS clear left inconsistent stats");
    kb::tests::Require(afterClear.chunkPoolInUse == 0U, "native ECS clear did not release chunk pool blocks");
    for (const kb::ecs::Entity entity : recreated) {
        kb::tests::Require(!storage.IsAlive(entity), "native ECS clear left a recreated entity alive");
    }
}

void RunClearRetainingCapacityTest() {
    constexpr std::size_t kEntityCount = 1024U;
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk4KB;
    config.reserveEntities = kEntityCount;
    config.reserveArchetypes = 2U;
    kb::ecs::NativeArchetypeStorage storage(config);

    std::vector<Position> positions;
    std::vector<Velocity> velocities;
    positions.reserve(kEntityCount);
    velocities.reserve(kEntityCount);
    for (std::size_t index = 0U; index < kEntityCount; ++index) {
        positions.push_back(Position{ .x = static_cast<float>(index), .y = static_cast<float>(index + 1U) });
        velocities.push_back(Velocity{ .x = static_cast<float>(index + 2U), .y = static_cast<float>(index + 3U) });
    }
    const std::array columns{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Position>(kPositionId),
            .data = positions.data(),
            .stride = sizeof(Position),
        },
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Velocity>(kVelocityId),
            .data = velocities.data(),
            .stride = sizeof(Velocity),
        },
    };

    std::vector<kb::ecs::Entity> entities;
    storage.CreateEntitiesInto(entities, kEntityCount, columns);
    const kb::ecs::NativeEcsStorageStats beforeClear = storage.Stats();
    RequireStorageStatsConsistent(beforeClear, kEntityCount, "native ECS retaining clear setup left inconsistent stats");
    kb::tests::Require(beforeClear.chunks > 1U, "native ECS retaining clear setup did not allocate multiple chunks");
    kb::tests::Require(beforeClear.chunkPoolFree == 0U, "native ECS retaining clear setup unexpectedly had free chunks");

    storage.ClearRetainingCapacity();
    const kb::ecs::NativeEcsStorageStats afterRetainClear = storage.Stats();
    kb::tests::Require(afterRetainClear.liveEntities == 0U, "native ECS retaining clear did not clear live entity count");
    kb::tests::Require(afterRetainClear.chunks == beforeClear.chunks, "native ECS retaining clear released table chunks");
    kb::tests::Require(storage.ChunkCount() == beforeClear.chunks, "native ECS retaining clear did not preserve chunk count");
    kb::tests::Require(afterRetainClear.capacity == beforeClear.capacity, "native ECS retaining clear did not preserve row capacity");
    kb::tests::Require(afterRetainClear.chunkPoolInUse == beforeClear.chunkPoolInUse, "native ECS retaining clear returned chunks to the pool");
    kb::tests::Require(afterRetainClear.chunkPoolFree == beforeClear.chunkPoolFree, "native ECS retaining clear changed the free pool");
    kb::tests::Require(afterRetainClear.emptyChunks == beforeClear.chunks, "native ECS retaining clear did not report retained chunks as empty");
    kb::tests::Require(afterRetainClear.usedBytes == 0U, "native ECS retaining clear left used payload bytes");
    for (const kb::ecs::Entity entity : entities) {
        kb::tests::Require(!storage.IsAlive(entity), "native ECS retaining clear left an entity alive");
    }

    std::vector<kb::ecs::Entity> recreated;
    storage.CreateEntitiesInto(recreated, kEntityCount, columns);
    const kb::ecs::NativeEcsStorageStats afterRecreate = storage.Stats();
    RequireStorageStatsConsistent(afterRecreate, kEntityCount, "native ECS retaining clear recreate left inconsistent stats");
    kb::tests::Require(afterRecreate.chunkPoolAcquireCount == afterRetainClear.chunkPoolAcquireCount, "native ECS retaining clear recreate acquired new chunks");
    kb::tests::Require(afterRecreate.chunkPoolInUse == beforeClear.chunkPoolInUse, "native ECS retaining clear recreate changed in-use chunk count");
    kb::tests::Require(recreated.size() == kEntityCount, "native ECS retaining clear recreate returned the wrong entity count");
    kb::tests::Require(
        kb::tests::NearlyEqual(Component<Position>(storage, recreated[127U], kPositionId).x, positions[127U].x),
        "native ECS retaining clear recreate wrote the wrong component row");

    storage.Clear();
    const kb::ecs::NativeEcsStorageStats afterReleaseClear = storage.Stats();
    RequireStorageStatsConsistent(afterReleaseClear, 0U, "native ECS release clear after retaining clear left inconsistent stats");
    kb::tests::Require(afterReleaseClear.chunkPoolInUse == 0U, "native ECS release clear after retaining clear kept chunks in use");
}

void RunBulkAdoptDuplicateValidationTest() {
    kb::ecs::NativeArchetypeStorage storage;
    std::array<Position, 4U> positions{
        Position{ .x = 1.0F, .y = 2.0F },
        Position{ .x = 3.0F, .y = 4.0F },
        Position{ .x = 5.0F, .y = 6.0F },
        Position{ .x = 7.0F, .y = 8.0F },
    };
    const std::array columns{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Position>(kPositionId),
            .data = positions.data(),
            .stride = sizeof(Position),
        },
    };

    const std::array sortedDuplicateBatch{
        kb::ecs::Entity{ 40'000'000ULL },
        kb::ecs::Entity{ 40'000'000ULL },
        kb::ecs::Entity{ 40'000'001ULL },
    };
    bool rejectedSortedDuplicate = false;
    try {
        storage.AdoptEntities(sortedDuplicateBatch, columns);
    } catch (const std::invalid_argument&) {
        rejectedSortedDuplicate = true;
    }
    kb::tests::Require(rejectedSortedDuplicate, "native ECS bulk adopt accepted a sorted duplicate entity batch");
    kb::tests::Require(storage.Stats().liveEntities == 0U, "native ECS sorted duplicate adopt partially mutated storage");

    const std::array unsortedDuplicateBatch{
        kb::ecs::Entity{ 40'000'002ULL },
        kb::ecs::Entity{ 40'000'004ULL },
        kb::ecs::Entity{ 40'000'002ULL },
    };
    bool rejectedUnsortedDuplicate = false;
    try {
        storage.AdoptEntities(unsortedDuplicateBatch, columns);
    } catch (const std::invalid_argument&) {
        rejectedUnsortedDuplicate = true;
    }
    kb::tests::Require(rejectedUnsortedDuplicate, "native ECS bulk adopt accepted an unsorted duplicate entity batch");
    kb::tests::Require(storage.Stats().liveEntities == 0U, "native ECS unsorted duplicate adopt partially mutated storage");
}

void RunBulkAdoptExternalResolveFastPathTest() {
    kb::ecs::NativeArchetypeStorage storage;
    const Position position{ .x = 1.0F, .y = 2.0F };
    const std::array columns{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Position>(kPositionId),
            .data = &position,
            .stride = sizeof(Position),
            .sourceCount = 1U,
        },
    };
    const std::array externalEntities{
        kb::ecs::Entity{ 46'000'000ULL },
        kb::ecs::Entity{ (static_cast<kb::ecs::Entity::IdType>(7U) << 32U) | 46'000'001ULL },
    };

    storage.AdoptEntities(externalEntities, columns);
    kb::tests::Require(storage.ResolveAliveEntity(externalEntities[0].Id()) == externalEntities[0], "native ECS external exact resolve failed");
    kb::tests::Require(storage.ResolveAliveEntity(46'000'001ULL) == externalEntities[1], "native ECS stripped external resolve failed");
    kb::tests::Require(storage.IsAlive(externalEntities[0]), "native ECS exact external slot did not keep entity alive");
    kb::tests::Require(storage.IsAlive(externalEntities[1]), "native ECS stripped external slot did not keep entity alive");

    storage.DestroyEntities(externalEntities);
    kb::tests::Require(!storage.ResolveAliveEntity(externalEntities[0].Id()).IsValid(), "native ECS external exact resolve survived destroy");
    kb::tests::Require(!storage.ResolveAliveEntity(46'000'001ULL).IsValid(), "native ECS stripped external resolve survived destroy");
}

void RunBulkAdoptContiguousExternalRangeFastPathTest() {
    kb::ecs::NativeArchetypeStorage storage;
    std::vector<Position> positions;
    std::vector<kb::ecs::Entity> externalEntities;
    positions.reserve(128U);
    externalEntities.reserve(128U);
    for (std::size_t index = 0; index < 128U; ++index) {
        positions.push_back(Position{ .x = static_cast<float>(index), .y = static_cast<float>(index + 10U) });
        externalEntities.push_back(kb::ecs::Entity{ 60'000'000ULL + static_cast<kb::ecs::Entity::IdType>(index) });
    }

    const std::array columns{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Position>(kPositionId),
            .data = positions.data(),
            .stride = sizeof(Position),
        },
    };

    storage.AdoptEntities(externalEntities, columns);
    for (std::size_t index = 0; index < externalEntities.size(); ++index) {
        const kb::ecs::Entity entity = externalEntities[index];
        kb::tests::Require(storage.ResolveAliveEntity(entity.Id()) == entity, "native ECS contiguous external range resolve failed");
        kb::tests::Require(storage.IsAlive(entity), "native ECS contiguous external range did not keep entity alive");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Position>(storage, entity, kPositionId).x, positions[index].x), "native ECS contiguous external range read the wrong row");
    }

    const kb::ecs::Entity recycledEntity = externalEntities[17U];
    storage.DestroyEntity(recycledEntity);
    kb::tests::Require(!storage.IsAlive(recycledEntity), "native ECS contiguous external range kept a destroyed entity alive");
    kb::tests::Require(!storage.ResolveAliveEntity(recycledEntity.Id()).IsValid(), "native ECS contiguous external range resolved a destroyed entity");

    const Position recycledPosition{ .x = 777.0F, .y = 888.0F };
    const std::array recycledColumns{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Position>(kPositionId),
            .data = &recycledPosition,
            .stride = sizeof(Position),
            .sourceCount = 1U,
        },
    };
    const std::array recycledBatch{ recycledEntity };
    storage.AdoptEntities(recycledBatch, recycledColumns);
    kb::tests::Require(storage.IsAlive(recycledEntity), "native ECS contiguous external range did not allow readopting a destroyed external entity");
    kb::tests::Require(kb::tests::NearlyEqual(Component<Position>(storage, recycledEntity, kPositionId).x, recycledPosition.x), "native ECS contiguous external range reused a stale record after readopt");

    storage.DestroyEntities(externalEntities);
    kb::tests::Require(storage.Stats().liveEntities == 0U, "native ECS contiguous external range full destroy did not clear live entities");
    for (const kb::ecs::Entity entity : externalEntities) {
        kb::tests::Require(!storage.ResolveAliveEntity(entity.Id()).IsValid(), "native ECS contiguous external range survived full destroy");
    }

    storage.AdoptEntities(externalEntities, columns);
    kb::tests::Require(storage.Stats().liveEntities == externalEntities.size(), "native ECS contiguous external range readopt did not restore all entities");
    for (std::size_t index = 0; index < externalEntities.size(); ++index) {
        const kb::ecs::Entity entity = externalEntities[index];
        kb::tests::Require(storage.ResolveAliveEntity(entity.Id()) == entity, "native ECS contiguous external range readopt resolve failed");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Position>(storage, entity, kPositionId).x, positions[index].x), "native ECS contiguous external range readopt read the wrong row");
    }

    storage.ClearRetainingCapacity();
    kb::tests::Require(storage.Stats().liveEntities == 0U, "native ECS contiguous external retaining clear did not clear live entities");
    for (const kb::ecs::Entity entity : externalEntities) {
        kb::tests::Require(!storage.IsAlive(entity), "native ECS contiguous external retaining clear kept a stale entity alive");
        kb::tests::Require(!storage.ResolveAliveEntity(entity.Id()).IsValid(), "native ECS contiguous external retaining clear resolved a stale entity");
    }

    std::vector<Position> readoptedPositions;
    readoptedPositions.reserve(externalEntities.size());
    for (std::size_t index = 0; index < externalEntities.size(); ++index) {
        readoptedPositions.push_back(Position{ .x = static_cast<float>(index + 1000U), .y = static_cast<float>(index + 2000U) });
    }
    const std::array readoptedColumns{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Position>(kPositionId),
            .data = readoptedPositions.data(),
            .stride = sizeof(Position),
        },
    };
    storage.AdoptEntities(externalEntities, readoptedColumns);
    kb::tests::Require(storage.Stats().liveEntities == externalEntities.size(), "native ECS contiguous external retaining clear readopt did not restore all entities");
    for (std::size_t index = 0; index < externalEntities.size(); ++index) {
        const kb::ecs::Entity entity = externalEntities[index];
        kb::tests::Require(storage.IsAlive(entity), "native ECS contiguous external retaining clear readopt produced a dead entity");
        kb::tests::Require(kb::tests::NearlyEqual(Component<Position>(storage, entity, kPositionId).x, readoptedPositions[index].x), "native ECS contiguous external retaining clear readopt reused stale component data");
    }
}

void RunNativeStorageStructuralVersionTest() {
    kb::ecs::NativeArchetypeStorage storage;
    const std::uint64_t initialVersion = storage.StructuralVersion();

    const Position position{ .x = 1.0F, .y = 2.0F };
    const std::array positionComponent{
        kb::ecs::NativeComponentValue{
            .type = ComponentType<Position>(kPositionId),
            .data = &position,
        },
    };
    const kb::ecs::Entity entity = storage.CreateEntity(positionComponent);
    const std::uint64_t afterCreate = storage.StructuralVersion();
    kb::tests::Require(afterCreate > initialVersion, "native ECS structural version did not advance after create");

    const Position overwritten{ .x = 3.0F, .y = 4.0F };
    storage.SetComponent(entity, kPositionId, &overwritten, sizeof(overwritten));
    kb::tests::Require(storage.StructuralVersion() == afterCreate, "native ECS structural version advanced after non-structural component write");

    const Velocity velocity{ .x = 5.0F, .y = 6.0F };
    const std::array velocityComponent{
        kb::ecs::NativeComponentValue{
            .type = ComponentType<Velocity>(kVelocityId),
            .data = &velocity,
        },
    };
    storage.AddComponents(entity, velocityComponent);
    const std::uint64_t afterAdd = storage.StructuralVersion();
    kb::tests::Require(afterAdd > afterCreate, "native ECS structural version did not advance after component add migration");

    const std::array removedIds{ kVelocityId };
    storage.RemoveComponents(entity, removedIds);
    const std::uint64_t afterRemove = storage.StructuralVersion();
    kb::tests::Require(afterRemove > afterAdd, "native ECS structural version did not advance after component remove migration");

    storage.DestroyEntity(entity);
    const std::uint64_t afterDestroy = storage.StructuralVersion();
    kb::tests::Require(afterDestroy > afterRemove, "native ECS structural version did not advance after destroy");

    std::vector<Position> positions(8);
    const std::array columns{
        kb::ecs::NativeBulkComponentColumn{
            .type = ComponentType<Position>(kPositionId),
            .data = positions.data(),
            .stride = sizeof(Position),
        },
    };
    const std::vector<kb::ecs::Entity> entities = storage.CreateEntities(positions.size(), columns);
    const std::uint64_t afterBulkCreate = storage.StructuralVersion();
    kb::tests::Require(afterBulkCreate > afterDestroy, "native ECS structural version did not advance after bulk create");

    storage.ClearRetainingCapacity();
    kb::tests::Require(storage.StructuralVersion() > afterBulkCreate, "native ECS structural version did not advance after retaining clear");
    for (kb::ecs::Entity cleared : entities) {
        kb::tests::Require(!storage.IsAlive(cleared), "native ECS structural version test clear left an entity alive");
    }
}

void RunHotArchetypeLayoutAdvisorTest() {
    kb::ecs::NativeEcsStorageStats stats;

    // Wide hot archetype that also carries cold columns and runs at low occupancy.
    kb::ecs::NativeEcsArchetypeMemoryCounters wide;
    wide.archetypeIndex = 1U;
    wide.componentIds = std::vector<kb::ecs::ComponentId>(10U, kb::ecs::ComponentId{ 1 });
    wide.liveEntities = 100U;
    wide.chunks = 4U;
    wide.capacity = 400U;
    wide.hotTableComponents = 9U;
    wide.coldTableComponents = 1U;
    wide.hotTableCapacityBytes = 400U * 300U; // 300 hot bytes per entity
    stats.archetypeCounters.push_back(wide);

    // Healthy compact archetype: should not trigger any advisory.
    kb::ecs::NativeEcsArchetypeMemoryCounters healthy;
    healthy.archetypeIndex = 2U;
    healthy.componentIds = std::vector<kb::ecs::ComponentId>(2U, kb::ecs::ComponentId{ 2 });
    healthy.liveEntities = 1000U;
    healthy.chunks = 2U;
    healthy.capacity = 1000U;
    healthy.hotTableComponents = 2U;
    healthy.hotTableCapacityBytes = 1000U * 16U;
    stats.archetypeCounters.push_back(healthy);

    const kb::ecs::HotArchetypeLayoutReport report = kb::ecs::AnalyzeHotArchetypeLayout(stats);
    kb::tests::Require(report.analyzedArchetypes == 2U, "advisor analyzed the wrong archetype count");
    kb::tests::Require(report.CountOfKind(kb::ecs::HotArchetypeAdvisoryKind::SplitLargeHotArchetype) == 1U, "advisor missed wide hot archetype");
    kb::tests::Require(report.CountOfKind(kb::ecs::HotArchetypeAdvisoryKind::ColdComponentsInHotArchetype) == 1U, "advisor missed cold-in-hot archetype");
    kb::tests::Require(report.CountOfKind(kb::ecs::HotArchetypeAdvisoryKind::LowOccupancyArchetype) == 1U, "advisor missed low-occupancy archetype");

    // Tag explosion: many near-empty tag-bearing archetypes.
    kb::ecs::NativeEcsStorageStats tagStats;
    for (std::size_t index = 0U; index < 32U; ++index) {
        kb::ecs::NativeEcsArchetypeMemoryCounters tagArchetype;
        tagArchetype.archetypeIndex = index;
        tagArchetype.componentIds = std::vector<kb::ecs::ComponentId>(1U, kb::ecs::ComponentId{ 3 });
        tagArchetype.liveEntities = 1U;
        tagArchetype.chunks = 1U;
        tagArchetype.capacity = 64U;
        tagArchetype.sparseTagComponents = 1U;
        tagStats.archetypeCounters.push_back(tagArchetype);
    }
    const kb::ecs::HotArchetypeLayoutReport tagReport = kb::ecs::AnalyzeHotArchetypeLayout(tagStats);
    kb::tests::Require(tagReport.CountOfKind(kb::ecs::HotArchetypeAdvisoryKind::TagArchetypeExplosion) == 1U, "advisor missed tag archetype explosion");
    kb::tests::Require(
        kb::ecs::HotArchetypeAdvisoryKindName(kb::ecs::HotArchetypeAdvisoryKind::TagArchetypeExplosion) == "tag_archetype_explosion",
        "advisor kind name mismatch");
}

} // namespace

namespace kb::tests {

void RunEcsNativeArchetypeStorageTests() {
    RunChunkProfileAndStatsTest();
    RunArchetypeCapacityReportTest();
    RunChunkPoolReuseAccountingTest();
    RunChunkPoolMaintenanceBudgetTest();
    RunChunkCommitGuardTest();
    RunGeneratedEntityResolveFastPathTest();
    RunMemoryCountersPerArchetypeAndChunkTest();
    RunMultiComponentMigrationTest();
    RunBulkRemoveComponentMigrationTest();
    RunBulkStructuralScratchReuseTest();
    RunBulkColumnSourceCountValidationTest();
    RunSwapDeleteAndGenerationTest();
    RunMoveLastCompactionAcrossChunksTest();
    RunStructuralChurnOccupancyTest();
    RunArchetypeSignatureMatchingTest();
    RunNativeBulkCreateAdoptColumnAppendTest();
    RunWorldNativeStorageMirrorTest();
    RunWorldDirectBulkCreateTest();
    RunWorldDirectBulkCreateIntoReuseTest();
    RunWorldDirectBulkBroadcastCreateTest();
    RunWorldDirectBulkPatternCreateTest();
    RunWorldNativeStorageMaintenanceTest();
    RunWorldBulkStorageStatsConsistencyTest();
    RunWorldBulkMappingIntegrityTest();
    RunWorldBulkVersioningTest();
    RunNativeComponentDirtyRangeTest();
    RunNativeComponentAlignmentTest();
    RunNativeComponentAlignmentValidationTest();
    RunBulkAdoptDuplicateValidationTest();
    RunBulkAdoptExternalResolveFastPathTest();
    RunBulkAdoptContiguousExternalRangeFastPathTest();
    RunBulkDestroyDuplicateValidationTest();
    RunBulkDestroyAllFastPathTest();
    RunClearRetainingCapacityTest();
    RunNativeStorageStructuralVersionTest();
    RunHotArchetypeLayoutAdvisorTest();
}

} // namespace kb::tests
