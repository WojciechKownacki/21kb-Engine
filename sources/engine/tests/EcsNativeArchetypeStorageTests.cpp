#include "EcsTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/CommandBuffer.hpp"
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
    std::size_t countedSparseChunks = 0;
    std::size_t countedTailSparseChunks = 0;
    std::size_t countedFragmentedChunks = 0;
    std::size_t countedEmptyChunks = 0;
    std::size_t countedUsedBytes = 0;
    std::size_t countedWastedBytes = 0;

    for (const kb::ecs::NativeEcsArchetypeMemoryCounters& archetype : stats.archetypeCounters) {
        std::size_t archetypeRows = 0;
        std::size_t archetypeUsedBytes = 0;
        std::size_t archetypeWastedBytes = 0;
        std::size_t archetypePayloadBytes = 0;
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
            kb::tests::Require(chunk.usedBytes + chunk.wastedBytes == chunk.payloadBytes, message);
        }
        kb::tests::Require(archetypeRows == archetype.liveEntities, message);
        kb::tests::Require(archetypeUsedBytes == archetype.usedBytes, message);
        kb::tests::Require(archetypeWastedBytes == archetype.wastedBytes, message);
        kb::tests::Require(archetypePayloadBytes == archetype.payloadBytes, message);

        countedChunks += archetype.chunks;
        countedLiveEntities += archetype.liveEntities;
        countedCapacity += archetype.capacity;
        countedUsedBytes += archetype.usedBytes;
        countedWastedBytes += archetype.wastedBytes;
    }

    kb::tests::Require(stats.archetypeCounters.size() == stats.archetypeCount, message);
    kb::tests::Require(stats.liveEntities == expectedLiveEntities, message);
    kb::tests::Require(countedLiveEntities == expectedLiveEntities, message);
    kb::tests::Require(stats.chunks == countedChunks, message);
    kb::tests::Require(stats.capacity == countedCapacity, message);
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
    kb::tests::Require(stats.usedBytes == countedUsedBytes, message);
    kb::tests::Require(stats.wastedBytes == countedWastedBytes, message);
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

    storage.DestroyEntity(first);
    const kb::ecs::NativeEcsStorageStats afterDestroy = storage.Stats();
    kb::tests::Require(afterDestroy.chunkPoolInUse == 0U, "native ECS chunk pool did not release the empty chunk");
    kb::tests::Require(storage.ChunkCount() == afterDestroy.chunks, "native ECS chunk count did not track chunk release");
    kb::tests::Require(afterDestroy.chunkPoolFree == 1U, "native ECS chunk pool did not retain a free chunk");
    kb::tests::Require(afterDestroy.chunkPoolReleaseCount == 1U, "native ECS chunk pool did not count release");

    [[maybe_unused]] const kb::ecs::Entity second = storage.CreateEntity(components);
    const kb::ecs::NativeEcsStorageStats afterReuse = storage.Stats();
    kb::tests::Require(afterReuse.chunkPoolAllocated == 1U, "native ECS chunk pool allocated instead of reusing a free chunk");
    kb::tests::Require(afterReuse.chunkPoolInUse == 1U, "native ECS chunk pool did not count reused chunk in use");
    kb::tests::Require(storage.ChunkCount() == afterReuse.chunks, "native ECS chunk count did not track chunk reuse");
    kb::tests::Require(afterReuse.chunkPoolFree == 0U, "native ECS chunk pool did not consume the free chunk");
    kb::tests::Require(afterReuse.chunkPoolAcquireCount == 2U, "native ECS chunk pool did not count second acquire");
    kb::tests::Require(afterReuse.chunkPoolReuseCount == 1U, "native ECS chunk pool did not count reuse");
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

    const kb::ecs::NativeEcsMaintenanceStats second = storage.MaintainChunks(kb::ecs::NativeEcsMaintenanceBudget{
        .maxFreeChunksToKeep = 0U,
    });
    kb::tests::Require(second.freeChunksAfter == 0U, "native ECS maintenance did not trim all free chunks");
    kb::tests::Require(!second.budgetExhausted, "native ECS maintenance reported budget exhaustion after full trim");
    kb::tests::Require(storage.Stats().chunkPoolTrimCount == first.chunksReleasedToSystem + second.chunksReleasedToSystem, "native ECS maintenance did not account trimmed chunks");
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
        kb::ecs::NativeComponentValue{ .type = ComponentType<Velocity>(kVelocityId), .data = &velocity },
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

        std::size_t archetypeRows = 0;
        std::size_t archetypeUsedBytes = 0;
        std::size_t archetypeWastedBytes = 0;
        std::size_t archetypePayloadBytes = 0;
        for (const kb::ecs::NativeEcsChunkMemoryCounters& chunk : archetype.chunkCounters) {
            kb::tests::Require(chunk.archetypeIndex == archetype.archetypeIndex, "native ECS memory counters recorded invalid chunk archetype index");
            kb::tests::Require(chunk.capacity >= chunk.liveEntities, "native ECS memory counters reported invalid chunk capacity");
            kb::tests::Require(chunk.payloadBytes == storage.ChunkPayloadBytes(), "native ECS memory counters recorded invalid chunk payload bytes");
            kb::tests::Require(chunk.usedBytes + chunk.wastedBytes == chunk.payloadBytes, "native ECS memory counters reported invalid chunk byte totals");
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
        }

        kb::tests::Require(archetypeRows == archetype.liveEntities, "native ECS memory counters did not sum chunk entity counts");
        kb::tests::Require(archetypeUsedBytes == archetype.usedBytes, "native ECS memory counters did not sum chunk used bytes");
        kb::tests::Require(archetypeWastedBytes == archetype.wastedBytes, "native ECS memory counters did not sum chunk wasted bytes");
        kb::tests::Require(archetypePayloadBytes == archetype.payloadBytes, "native ECS memory counters did not sum chunk payload bytes");

        countedChunks += archetype.chunks;
        countedCapacity += archetype.capacity;
        countedUsedBytes += archetype.usedBytes;
        countedWastedBytes += archetype.wastedBytes;
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
    kb::tests::Require(positionArchetype != nullptr && positionArchetype->usedBytes == sizeof(Position), "native ECS memory counters missed the position archetype");
    kb::tests::Require(movingArchetype != nullptr && movingArchetype->usedBytes == sizeof(Position) + sizeof(Velocity), "native ECS memory counters missed the moving archetype");
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
        kb::ecs::NativeComponentValue{ .type = ComponentType<Velocity>(kVelocityId), .data = &velocity },
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

    const std::array massComponent{
        kb::ecs::NativeComponentValue{ .type = ComponentType<Mass>(kMassId), .data = &mass },
    };
    storage.AddComponents(positionOnly, massComponent);
    const std::array positionMassQuery{ kPositionId, kMassId };
    kb::tests::Require(storage.EntityArchetypeMatches(positionOnly, positionMassQuery), "native ECS signature was not updated after add-component migration");

    const std::array removedMass{ kMassId };
    storage.RemoveComponents(positionOnly, removedMass);
    kb::tests::Require(!storage.EntityArchetypeMatches(positionOnly, positionMassQuery), "native ECS signature was not updated after remove-component migration");
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

} // namespace

namespace kb::tests {

void RunEcsNativeArchetypeStorageTests() {
    RunChunkProfileAndStatsTest();
    RunChunkPoolReuseAccountingTest();
    RunChunkPoolMaintenanceBudgetTest();
    RunGeneratedEntityResolveFastPathTest();
    RunMemoryCountersPerArchetypeAndChunkTest();
    RunMultiComponentMigrationTest();
    RunBulkRemoveComponentMigrationTest();
    RunBulkStructuralScratchReuseTest();
    RunBulkColumnSourceCountValidationTest();
    RunSwapDeleteAndGenerationTest();
    RunMoveLastCompactionAcrossChunksTest();
    RunArchetypeSignatureMatchingTest();
    RunNativeBulkCreateAdoptColumnAppendTest();
    RunWorldNativeStorageMirrorTest();
    RunWorldDirectBulkCreateTest();
    RunWorldDirectBulkBroadcastCreateTest();
    RunWorldDirectBulkPatternCreateTest();
    RunWorldNativeStorageMaintenanceTest();
    RunWorldBulkStorageStatsConsistencyTest();
    RunWorldBulkMappingIntegrityTest();
    RunWorldBulkVersioningTest();
    RunNativeComponentAlignmentTest();
    RunNativeComponentAlignmentValidationTest();
}

} // namespace kb::tests
