#include "EcsTestSuites.hpp"
#include "TestSupport.hpp"

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
    RunMultiComponentMigrationTest();
    RunSwapDeleteAndGenerationTest();
    RunArchetypeSignatureMatchingTest();
    RunWorldNativeStorageMirrorTest();
    RunNativeComponentAlignmentTest();
    RunNativeComponentAlignmentValidationTest();
}

} // namespace kb::tests
