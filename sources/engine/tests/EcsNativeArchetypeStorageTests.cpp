#include "EcsTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/CommandBuffer.hpp"
#include "engine/ecs/NativeArchetypeStorage.hpp"
#include "engine/ecs/World.hpp"

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

[[nodiscard]] bool IsAligned(const void* pointer, std::size_t alignment) noexcept {
    return pointer != nullptr && (reinterpret_cast<std::uintptr_t>(pointer) % alignment) == 0U;
}

void RequireStorageStatsConsistent(const kb::ecs::NativeEcsStorageStats& stats, std::size_t expectedLiveEntities, const char* message) {
    std::size_t countedChunks = 0;
    std::size_t countedLiveEntities = 0;
    std::size_t countedUsedBytes = 0;
    std::size_t countedWastedBytes = 0;

    for (const kb::ecs::NativeEcsArchetypeMemoryCounters& archetype : stats.archetypeCounters) {
        std::size_t archetypeRows = 0;
        std::size_t archetypeUsedBytes = 0;
        std::size_t archetypeWastedBytes = 0;
        std::size_t archetypePayloadBytes = 0;
        for (const kb::ecs::NativeEcsChunkMemoryCounters& chunk : archetype.chunkCounters) {
            archetypeRows += chunk.liveEntities;
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
        countedUsedBytes += archetype.usedBytes;
        countedWastedBytes += archetype.wastedBytes;
    }

    kb::tests::Require(stats.archetypeCounters.size() == stats.archetypeCount, message);
    kb::tests::Require(stats.liveEntities == expectedLiveEntities, message);
    kb::tests::Require(countedLiveEntities == expectedLiveEntities, message);
    kb::tests::Require(stats.chunks == countedChunks, message);
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
    kb::tests::Require(stats.usedBytes >= sizeof(Position), "native ECS storage stats did not count used component bytes");
    kb::tests::Require(stats.wastedBytes < storage.ChunkPayloadBytes(), "native ECS storage reported invalid wasted bytes");

    storage.DestroyEntity(entity);
    const kb::ecs::NativeEcsStorageStats emptyStats = storage.Stats();
    kb::tests::Require(!storage.IsAlive(entity), "native ECS storage did not invalidate a destroyed entity");
    kb::tests::Require(emptyStats.liveEntities == 0, "native ECS storage stats did not drop destroyed entities");
    kb::tests::Require(emptyStats.chunks == 0, "native ECS storage did not return an empty chunk to the free list");
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
        countedUsedBytes += archetype.usedBytes;
        countedWastedBytes += archetype.wastedBytes;
    }

    kb::tests::Require(stats.archetypeCounters.size() == stats.archetypeCount, "native ECS memory counters omitted archetype counters");
    kb::tests::Require(stats.chunks == countedChunks, "native ECS memory counters did not sum storage chunk count");
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
    RunMemoryCountersPerArchetypeAndChunkTest();
    RunMultiComponentMigrationTest();
    RunSwapDeleteAndGenerationTest();
    RunArchetypeSignatureMatchingTest();
    RunNativeBulkCreateAdoptColumnAppendTest();
    RunWorldNativeStorageMirrorTest();
    RunWorldBulkStorageStatsConsistencyTest();
    RunWorldBulkMappingIntegrityTest();
    RunWorldBulkVersioningTest();
    RunNativeComponentAlignmentTest();
    RunNativeComponentAlignmentValidationTest();
}

} // namespace kb::tests
