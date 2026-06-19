#include "EcsTestTypes.hpp"
#include "EcsTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/ComponentReflectionMacros.hpp"
#include "engine/ecs/World.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

struct EcsLifetimeValidationTag {};
struct EcsLifetimeValidationRelation {};
struct EcsChurnMass {
    float value = 0.0F;
};
struct EcsChurnPayload {
    int value = 0;
};
struct EcsStorageDefaultPayload {
    float value = 0.0F;
};
struct EcsStorageColdPayload {
    float value = 0.0F;
};
struct EcsStorageSparseTagPayload {
    std::uint32_t value = 0U;
};
struct EcsStorageSparsePayload {
    std::uint32_t value = 0U;
};
struct EcsStorageSharedPayload {
    std::uint64_t value = 0U;
};
struct EcsStorageExternalPayload {
    std::uint64_t value = 0U;
};
struct EcsStorageBulkColdPayload {
    float value = 0.0F;
};
struct EcsStorageBulkSharedPayload {
    std::uint32_t value = 0U;
};

void CountPositions(kb::ecs::Entity entity, const EcsPosition& position, void* context) {
    static_cast<void>(entity);
    auto* counters = static_cast<EcsIterationCounters*>(context);
    ++counters->visited;
    counters->sumX += position.x;
}

void ApplyVelocity(kb::ecs::Entity entity, EcsPosition& position, void* context) {
    auto* world = static_cast<kb::ecs::World*>(context);
    const EcsVelocity* velocity = world->TryGet<EcsVelocity>(entity);
    if (velocity != nullptr) {
        position.x += velocity->x;
        position.y += velocity->y;
    }
}

void CountMovingPositions(kb::ecs::Entity entity, const EcsPosition& position, const EcsVelocity& velocity, void* context) {
    static_cast<void>(entity);
    auto* counters = static_cast<EcsIterationCounters*>(context);
    ++counters->visited;
    counters->sumX += position.x + velocity.x;
}

void CountMovingBatches(const kb::ecs::QueryBatch<EcsPosition, EcsVelocity>& batch, void* context) {
    auto* counters = static_cast<EcsIterationCounters*>(context);
    const EcsPosition* positions = batch.Components<0>();
    const EcsVelocity* velocities = batch.Components<1>();
    for (std::size_t row = 0; row < batch.Count(); ++row) {
        ++counters->visited;
        counters->sumX += positions[row].x + velocities[row].x;
    }
}

void CountMassBatches(const kb::ecs::QueryBatch<EcsPosition, EcsChurnMass>& batch, void* context) {
    auto* counters = static_cast<EcsIterationCounters*>(context);
    const EcsPosition* positions = batch.Components<0>();
    const EcsChurnMass* masses = batch.Components<1>();
    for (std::size_t row = 0; row < batch.Count(); ++row) {
        ++counters->visited;
        counters->sumX += positions[row].x + masses[row].value;
    }
}

template <typename Fn>
[[nodiscard]] bool ThrowsStaleEntity(Fn&& fn) {
    try {
        fn();
    } catch (const std::out_of_range&) {
        return true;
    }
    return false;
}

void RunTypedEcsComponentApiTest() {
    kb::ecs::World world;
    const kb::ecs::ComponentId positionComponent = world.RegisterComponent<EcsPosition>("test.EcsPosition");
    const kb::ecs::ComponentId samePositionComponent = world.RegisterComponent<EcsPosition>("test.EcsPosition");
    kb::tests::Require(positionComponent != 0, "Typed ECS component registration failed");
    kb::tests::Require(positionComponent == samePositionComponent, "Typed ECS component registration was not cached per type");

    const kb::ecs::Entity entity = world.CreateEntity("Mover");
    world.Set(entity, EcsPosition{ .x = 2.0F, .y = 3.0F });
    world.Set(entity, EcsVelocity{ .x = 4.0F, .y = -1.0F });

    kb::tests::Require(world.Has<EcsPosition>(entity), "Typed ECS component was not assigned");
    kb::tests::Require(world.Has<EcsVelocity>(entity), "Second typed ECS component was not assigned");
    const EcsPosition* position = world.TryGet<EcsPosition>(entity);
    kb::tests::Require(position != nullptr && kb::tests::NearlyEqual(position->x, 2.0F) && kb::tests::NearlyEqual(position->y, 3.0F), "Typed ECS component read failed");

    EcsPosition* mutablePosition = world.TryGetMutable<EcsPosition>(entity);
    kb::tests::Require(mutablePosition != nullptr, "Typed ECS mutable component read failed");
    mutablePosition->x = 5.0F;
    world.MarkModified<EcsPosition>(entity);

    EcsIterationCounters counters;
    world.ForEach<EcsPosition>(&CountPositions, &counters);
    kb::tests::Require(counters.visited == 1, "Typed ECS const iteration did not visit the component");
    kb::tests::Require(kb::tests::NearlyEqual(counters.sumX, 5.0F), "Typed ECS const iteration saw invalid component data");

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    world.ForEachMutable<EcsPosition>(&ApplyVelocity, &world);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
    position = world.TryGet<EcsPosition>(entity);
    kb::tests::Require(position != nullptr && kb::tests::NearlyEqual(position->x, 9.0F) && kb::tests::NearlyEqual(position->y, 2.0F), "Typed ECS mutable iteration did not update component data");

    const kb::ecs::Entity stationaryEntity = world.CreateEntity("Stationary");
    world.Set(stationaryEntity, EcsPosition{ .x = 100.0F, .y = 200.0F });

    EcsIterationCounters queryCounters;
    world.ForEach<EcsPosition, EcsVelocity>(&CountMovingPositions, &queryCounters);
    kb::tests::Require(queryCounters.visited == 1, "Typed ECS two-component query did not filter the partial archetype");
    kb::tests::Require(kb::tests::NearlyEqual(queryCounters.sumX, 13.0F), "Typed ECS two-component query saw invalid component data");

    kb::ecs::Query<EcsPosition, EcsVelocity> movingQuery = world.CreateQuery<EcsPosition, EcsVelocity>();
    kb::tests::Require(movingQuery.IsValid(), "Persistent typed ECS query was not created");
    EcsIterationCounters persistentQueryCounters;
    movingQuery.ForEach(&CountMovingPositions, &persistentQueryCounters);
    kb::tests::Require(persistentQueryCounters.visited == 1, "Persistent typed ECS query did not visit matching entity");
    kb::tests::Require(kb::tests::NearlyEqual(persistentQueryCounters.sumX, 13.0F), "Persistent typed ECS query saw invalid component data");

    world.Remove<EcsVelocity>(entity);
    kb::tests::Require(!world.Has<EcsVelocity>(entity), "Typed ECS component remove failed");

    EcsIterationCounters removedQueryCounters;
    movingQuery.ForEach(&CountMovingPositions, &removedQueryCounters);
    kb::tests::Require(removedQueryCounters.visited == 0, "Persistent typed ECS query did not react to removed component");
}

void RunTypedEcsComponentStoragePolicyTest() {
    kb::ecs::World world;

    const kb::ecs::ComponentId defaultComponent = world.RegisterComponent<EcsStorageDefaultPayload>("test.StorageDefault");
    const kb::ecs::ComponentId coldComponent = world.RegisterComponent<EcsStorageColdPayload>(
        "test.StorageCold",
        kb::ecs::ComponentRegistrationOptions{ .storageClass = kb::ecs::ComponentStorageClass::ColdTable });
    const kb::ecs::ComponentId sparseTagComponent = world.RegisterComponent<EcsStorageSparseTagPayload>(
        "test.StorageSparseTag",
        kb::ecs::ComponentRegistrationOptions{ .storageClass = kb::ecs::ComponentStorageClass::SparseTag });
    const kb::ecs::ComponentId sparsePayloadComponent = world.RegisterComponent<EcsStorageSparsePayload>(
        "test.StorageSparsePayload",
        kb::ecs::ComponentRegistrationOptions{ .storageClass = kb::ecs::ComponentStorageClass::SparsePayload });
    const kb::ecs::ComponentId sharedComponent = world.RegisterComponent<EcsStorageSharedPayload>(
        "test.StorageShared",
        kb::ecs::ComponentRegistrationOptions{ .storageClass = kb::ecs::ComponentStorageClass::SharedValue });
    const kb::ecs::ComponentId externalComponent = world.RegisterComponent<EcsStorageExternalPayload>(
        "test.StorageExternal",
        kb::ecs::ComponentRegistrationOptions{ .storageClass = kb::ecs::ComponentStorageClass::ExternalBlob });

    kb::tests::Require(world.ComponentStorage(defaultComponent) == kb::ecs::ComponentStorageClass::HotTable, "ECS component storage default must be hot table");
    kb::tests::Require(world.ComponentStorage<EcsStorageDefaultPayload>() == kb::ecs::ComponentStorageClass::HotTable, "ECS typed component storage default must be hot table");
    kb::tests::Require(world.ComponentStorage(coldComponent) == kb::ecs::ComponentStorageClass::ColdTable, "ECS component cold storage metadata was not preserved");
    kb::tests::Require(world.ComponentStorage<EcsStorageColdPayload>() == kb::ecs::ComponentStorageClass::ColdTable, "ECS typed cold storage metadata was not preserved");
    kb::tests::Require(world.ComponentStorage(sparseTagComponent) == kb::ecs::ComponentStorageClass::SparseTag, "ECS sparse tag storage metadata was not preserved");
    kb::tests::Require(world.ComponentStorage(sparsePayloadComponent) == kb::ecs::ComponentStorageClass::SparsePayload, "ECS sparse payload storage metadata was not preserved");
    kb::tests::Require(world.ComponentStorage(sharedComponent) == kb::ecs::ComponentStorageClass::SharedValue, "ECS shared storage metadata was not preserved");
    kb::tests::Require(world.ComponentStorage(externalComponent) == kb::ecs::ComponentStorageClass::ExternalBlob, "ECS external storage metadata was not preserved");
    kb::tests::Require(world.ComponentStorage(0) == kb::ecs::ComponentStorageClass::HotTable, "ECS unknown component storage must be a stable hot-table fallback");

    const kb::ecs::ComponentId sameColdComponent = world.RegisterComponent<EcsStorageColdPayload>();
    kb::tests::Require(sameColdComponent == coldComponent, "ECS component storage registration must preserve per-type cache identity");
    kb::tests::Require(world.ComponentStorage<EcsStorageColdPayload>() == kb::ecs::ComponentStorageClass::ColdTable, "ECS cached registration must not reset storage metadata");

    const kb::ecs::Entity entity = world.CreateEntity("StoragePolicy");
    world.Set(entity, EcsStorageColdPayload{ .value = 4.0F });
    const kb::ecs::NativeEcsStorageStats storageStats = world.NativeStorageStats();
    kb::tests::Require(storageStats.coldTableComponents == 1U, "ECS native storage stats did not receive cold component metadata from World");
    kb::tests::Require(storageStats.hotTableComponents == 0U, "ECS native storage stats reported unexpected hot components for a cold-only entity");

    kb::ecs::World bulkWorld;
    const std::array bulkColdPayloads{
        EcsStorageBulkColdPayload{ .value = 1.0F },
        EcsStorageBulkColdPayload{ .value = 2.0F },
        EcsStorageBulkColdPayload{ .value = 3.0F },
    };
    const EcsStorageBulkSharedPayload sharedPayload{ .value = 7U };
    const std::array bulkViews{
        kb::ecs::World::MakeBulkComponentView<EcsStorageBulkColdPayload>(
            std::span<const EcsStorageBulkColdPayload>{ bulkColdPayloads },
            kb::ecs::ComponentRegistrationOptions{ .storageClass = kb::ecs::ComponentStorageClass::ColdTable }),
        kb::ecs::World::MakeBulkComponentBroadcastView<EcsStorageBulkSharedPayload>(
            sharedPayload,
            kb::ecs::ComponentRegistrationOptions{ .storageClass = kb::ecs::ComponentStorageClass::SharedValue }),
    };
    const std::vector<kb::ecs::Entity> bulkEntities = bulkWorld.CreateEntitiesNativeOnly(bulkColdPayloads.size(), bulkViews);
    kb::tests::Require(bulkEntities.size() == bulkColdPayloads.size(), "ECS bulk component storage policy setup did not create entities");
    kb::tests::Require(
        bulkWorld.ComponentStorage<EcsStorageBulkColdPayload>() == kb::ecs::ComponentStorageClass::ColdTable,
        "ECS bulk component view did not preserve cold table registration options");
    kb::tests::Require(
        bulkWorld.ComponentStorage<EcsStorageBulkSharedPayload>() == kb::ecs::ComponentStorageClass::SharedValue,
        "ECS bulk broadcast component view did not preserve shared value registration options");
    const kb::ecs::NativeEcsStorageStats bulkStats = bulkWorld.NativeStorageStats();
    kb::tests::Require(bulkStats.coldTableComponents == 1U, "ECS bulk native storage stats missed cold component metadata");
    kb::tests::Require(bulkStats.sharedValueComponents == 1U, "ECS bulk native storage stats missed shared component metadata");
    kb::tests::Require(
        bulkStats.coldTableUsedBytes == bulkColdPayloads.size() * sizeof(EcsStorageBulkColdPayload),
        "ECS bulk native storage stats reported invalid cold component bytes");
    kb::tests::Require(
        bulkStats.sharedValueUsedBytes == bulkColdPayloads.size() * sizeof(EcsStorageBulkSharedPayload),
        "ECS bulk native storage stats reported invalid shared component bytes");
    kb::tests::Require(bulkStats.activeSidePayloadBytes > 0U, "ECS bulk native storage stats missed side payload bytes");

    kb::tests::Require(kb::ecs::IsArchetypeTableStorage(kb::ecs::ComponentStorageClass::HotTable), "ECS hot storage must be table-backed");
    kb::tests::Require(kb::ecs::IsArchetypeTableStorage(kb::ecs::ComponentStorageClass::ColdTable), "ECS cold storage classifier failed");
    kb::tests::Require(kb::ecs::UsesHotChunkPayload(kb::ecs::ComponentStorageClass::HotTable), "ECS hot storage must use primary chunk payload");
    kb::tests::Require(!kb::ecs::UsesNativeSideStorage(kb::ecs::ComponentStorageClass::HotTable), "ECS hot storage must not use side payload");
    kb::tests::Require(!kb::ecs::UsesHotChunkPayload(kb::ecs::ComponentStorageClass::ColdTable), "ECS cold storage must not use primary hot payload");
    kb::tests::Require(kb::ecs::UsesNativeSideStorage(kb::ecs::ComponentStorageClass::ColdTable), "ECS cold storage must use native side payload");
    kb::tests::Require(kb::ecs::UsesNativeSideStorage(kb::ecs::ComponentStorageClass::SharedValue), "ECS shared storage must use native side payload");
    kb::tests::Require(kb::ecs::IsSparseStorage(kb::ecs::ComponentStorageClass::SparseTag), "ECS sparse tag storage classifier failed");
    kb::tests::Require(kb::ecs::IsSparseStorage(kb::ecs::ComponentStorageClass::SparsePayload), "ECS sparse payload storage classifier failed");
}

void RunTypedEcsEntityAndComponentLifetimeValidationTest() {
    kb::ecs::World world;
    kb::tests::Require(world.RegisterTag<EcsLifetimeValidationTag>("test.LifetimeValidationTag") != 0, "ECS lifetime validation tag registration failed");
    kb::tests::Require(world.RegisterRelation<EcsLifetimeValidationRelation>("test.LifetimeValidationRelation") != 0, "ECS lifetime validation relation registration failed");
    kb::tests::Require(
        world.RegisterComponentReflection<EcsPosition>(
            "test.EcsPosition",
            {
                KB_ECS_FIELD(EcsPosition, x, kb::ecs::ComponentFieldType::Float32),
                KB_ECS_FIELD(EcsPosition, y, kb::ecs::ComponentFieldType::Float32),
            }) != nullptr,
        "ECS lifetime validation component reflection registration failed");
    const kb::ecs::Entity entity = world.CreateEntity("Lifetime");
    const kb::ecs::Entity target = world.CreateEntity("LifetimeTarget");
    world.Set(entity, EcsPosition{ .x = 1.0F, .y = 2.0F });
    world.Set(entity, EcsVelocity{ .x = 3.0F, .y = 4.0F });
    world.AddTag<EcsLifetimeValidationTag>(entity);
    world.AddRelation<EcsLifetimeValidationRelation>(entity, target);
    world.SetParent(entity, target);

    world.Remove<EcsVelocity>(entity);
    kb::tests::Require(!world.Has<EcsVelocity>(entity), "ECS component lifetime validation kept a removed component alive");
    kb::tests::Require(world.TryGet<EcsVelocity>(entity) == nullptr, "ECS component lifetime validation returned removed component storage");
    kb::tests::Require(world.TryGetMutable<EcsVelocity>(entity) == nullptr, "ECS component lifetime validation returned mutable removed component storage");

    world.DestroyEntity(entity);
    kb::tests::Require(!world.IsAlive(entity), "ECS entity generation validation kept a destroyed entity alive");

    bool staleReadRejected = false;
    try {
        static_cast<void>(world.Has<EcsPosition>(entity));
    } catch (const std::out_of_range&) {
        staleReadRejected = true;
    }
    kb::tests::Require(staleReadRejected, "ECS entity generation validation accepted a stale read handle");

    bool staleWriteRejected = false;
    try {
        world.Set(entity, EcsVelocity{ .x = 5.0F, .y = 6.0F });
    } catch (const std::out_of_range&) {
        staleWriteRejected = true;
    }
    kb::tests::Require(staleWriteRejected, "ECS entity generation validation accepted a stale write handle");

    kb::tests::Require(ThrowsStaleEntity([&world, entity] { static_cast<void>(world.TryGet<EcsPosition>(entity)); }), "ECS stale validation accepted TryGet");
    kb::tests::Require(ThrowsStaleEntity([&world, entity] { static_cast<void>(world.TryGetMutable<EcsPosition>(entity)); }), "ECS stale validation accepted TryGetMutable");
    kb::tests::Require(ThrowsStaleEntity([&world, entity] { world.Remove<EcsPosition>(entity); }), "ECS stale validation accepted component remove");
    kb::tests::Require(ThrowsStaleEntity([&world, entity] { world.MarkModified<EcsPosition>(entity); }), "ECS stale validation accepted component mark modified");
    kb::tests::Require(ThrowsStaleEntity([&world, entity] { static_cast<void>(world.Name(entity)); }), "ECS stale validation accepted entity name read");
    kb::tests::Require(ThrowsStaleEntity([&world, entity] { world.SetName(entity, "Stale"); }), "ECS stale validation accepted entity rename");
    kb::tests::Require(ThrowsStaleEntity([&world, entity] { world.AddTag<EcsLifetimeValidationTag>(entity); }), "ECS stale validation accepted tag add");
    kb::tests::Require(ThrowsStaleEntity([&world, entity] { static_cast<void>(world.HasTag<EcsLifetimeValidationTag>(entity)); }), "ECS stale validation accepted tag read");
    kb::tests::Require(ThrowsStaleEntity([&world, entity] { world.RemoveTag<EcsLifetimeValidationTag>(entity); }), "ECS stale validation accepted tag remove");
    kb::tests::Require(ThrowsStaleEntity([&world, entity, target] { world.AddRelation<EcsLifetimeValidationRelation>(entity, target); }), "ECS stale validation accepted relation add source");
    kb::tests::Require(ThrowsStaleEntity([&world, entity, target] { world.AddRelation<EcsLifetimeValidationRelation>(target, entity); }), "ECS stale validation accepted relation add target");
    kb::tests::Require(ThrowsStaleEntity([&world, entity, target] { static_cast<void>(world.HasRelation<EcsLifetimeValidationRelation>(entity, target)); }), "ECS stale validation accepted relation read");
    kb::tests::Require(ThrowsStaleEntity([&world, entity, target] { world.RemoveRelation<EcsLifetimeValidationRelation>(entity, target); }), "ECS stale validation accepted relation remove");
    kb::tests::Require(ThrowsStaleEntity([&world, entity] { static_cast<void>(world.RelationTarget<EcsLifetimeValidationRelation>(entity)); }), "ECS stale validation accepted relation target read");
    kb::tests::Require(ThrowsStaleEntity([&world, entity, target] { world.SetParent(entity, target); }), "ECS stale validation accepted parent assignment");
    kb::tests::Require(ThrowsStaleEntity([&world, entity] { world.ClearParent(entity); }), "ECS stale validation accepted parent clear");
    kb::tests::Require(ThrowsStaleEntity([&world, entity] { static_cast<void>(world.Parent(entity)); }), "ECS stale validation accepted parent read");
    kb::tests::Require(ThrowsStaleEntity([&world, entity] { static_cast<void>(world.Children(entity)); }), "ECS stale validation accepted children read");
    kb::tests::Require(ThrowsStaleEntity([&world, entity] { static_cast<void>(world.InspectEntity(entity)); }), "ECS stale validation accepted inspection");
    kb::tests::Require(
        ThrowsStaleEntity([&world, entity] {
            kb::ecs::SerializedComponent serialized;
            static_cast<void>(world.SerializeComponent(entity, world.Component<EcsPosition>(), serialized));
        }),
        "ECS stale validation accepted serialized component read");
    kb::tests::Require(
        ThrowsStaleEntity([&world, entity] {
            kb::ecs::SerializedComponent serialized;
            static_cast<void>(world.ApplySerializedComponent(entity, serialized));
        }),
        "ECS stale validation accepted serialized component apply");

    bool invalidDestroyRejected = false;
    try {
        world.DestroyEntity(kb::ecs::Entity{});
    } catch (const std::invalid_argument&) {
        invalidDestroyRejected = true;
    }
    kb::tests::Require(invalidDestroyRejected, "ECS entity generation validation accepted an invalid destroy handle");
}

void RunTypedEcsBulkDestroyValidationTest() {
    kb::ecs::World world;
    std::vector<kb::ecs::Entity> entities;
    entities.reserve(4U);
    for (int index = 0; index < 4; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 0.0F });
        entities.push_back(entity);
    }

    world.DestroyEntities(std::span<const kb::ecs::Entity>{ entities.data(), 2U });
    kb::tests::Require(!world.IsAlive(entities[0]) && !world.IsAlive(entities[1]), "ECS bulk destroy kept destroyed entities alive");
    kb::tests::Require(world.IsAlive(entities[2]) && world.IsAlive(entities[3]), "ECS bulk destroy destroyed an entity outside the batch");

    bool rejectedStale = false;
    const std::array invalidBatch{ entities[2], entities[0] };
    try {
        world.DestroyEntities(std::span<const kb::ecs::Entity>{ invalidBatch });
    } catch (const std::out_of_range&) {
        rejectedStale = true;
    }
    kb::tests::Require(rejectedStale, "ECS bulk destroy accepted a stale entity");
    kb::tests::Require(world.IsAlive(entities[2]), "ECS bulk destroy partially mutated before rejecting a stale entity");

    world.DestroyEntities(std::span<const kb::ecs::Entity>{ entities.data() + 2U, 2U });
    kb::tests::Require(!world.IsAlive(entities[2]) && !world.IsAlive(entities[3]), "ECS bulk destroy failed after rejecting an earlier invalid batch");
}

struct StressEntityState {
    kb::ecs::Entity entity;
    bool hasVelocity = false;
    bool hasMass = false;
    bool hasMarker = false;
    bool hasPayload = false;
    float positionX = 0.0F;
};

[[nodiscard]] std::uint32_t NextStressValue(std::uint32_t& state) noexcept {
    state = (state * 1664525U) + 1013904223U;
    return state;
}

void RunTypedEcsRandomStructuralStressTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 8,
    });
    std::vector<StressEntityState> alive;
    alive.reserve(128);
    std::uint32_t randomState = 0x21CB2026U;

    for (int frame = 0; frame < 96; ++frame) {
        for (int operation = 0; operation < 48; ++operation) {
            const std::uint32_t value = NextStressValue(randomState);
            const std::uint32_t choice = value % 4U;
            if (choice == 0U || alive.empty()) {
                const float positionX = static_cast<float>((value >> 8U) & 0xFFU);
                const kb::ecs::Entity entity = world.CreateEntity();
                world.Set(entity, EcsPosition{ .x = positionX, .y = 0.0F });
                alive.push_back(StressEntityState{
                    .entity = entity,
                    .positionX = positionX,
                });
                continue;
            }

            const std::size_t index = static_cast<std::size_t>(value % alive.size());
            StressEntityState& state = alive[index];
            if (choice == 1U) {
                if (!state.hasVelocity) {
                    world.Set(state.entity, EcsVelocity{ .x = 1.0F, .y = 0.0F });
                    state.hasVelocity = true;
                } else {
                    world.Remove<EcsVelocity>(state.entity);
                    state.hasVelocity = false;
                }
            } else if (choice == 2U) {
                state.positionX += 1.0F;
                world.Set(state.entity, EcsPosition{ .x = state.positionX, .y = 0.0F });
            } else {
                const kb::ecs::Entity destroyed = state.entity;
                world.DestroyEntity(destroyed);
                alive[index] = alive.back();
                alive.pop_back();
                kb::tests::Require(!world.IsAlive(destroyed), "ECS stress destroy left an entity alive");
            }
        }

        int expectedMoving = 0;
        float expectedSum = 0.0F;
        for (const StressEntityState& state : alive) {
            kb::tests::Require(world.IsAlive(state.entity), "ECS stress model contains a dead live entity");
            kb::tests::Require(world.Has<EcsPosition>(state.entity), "ECS stress entity lost its required position");
            if (state.hasVelocity) {
                ++expectedMoving;
                expectedSum += state.positionX + 1.0F;
                kb::tests::Require(world.Has<EcsVelocity>(state.entity), "ECS stress entity lost velocity");
            } else {
                kb::tests::Require(!world.Has<EcsVelocity>(state.entity), "ECS stress entity retained removed velocity");
            }
        }

        EcsIterationCounters counters;
        kb::ecs::Query<EcsPosition, EcsVelocity> query = world.CreateQuery<EcsPosition, EcsVelocity>();
        query.ForEach(&CountMovingPositions, &counters);
        kb::tests::Require(counters.visited == expectedMoving, "ECS stress query returned an unexpected moving entity count");
        kb::tests::Require(kb::tests::NearlyEqual(counters.sumX, expectedSum), "ECS stress query returned unexpected component data");
    }
}

void RunTypedEcsArchetypeChurnStressTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 5,
    });

    std::vector<StressEntityState> alive;
    alive.reserve(192);
    std::uint32_t randomState = 0xA21CB17U;

    auto createEntity = [&world, &alive](float positionX, std::uint32_t mask) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = positionX, .y = 0.0F });
        StressEntityState state{
            .entity = entity,
            .positionX = positionX,
        };
        if ((mask & 0x1U) != 0U) {
            world.Set(entity, EcsVelocity{ .x = 2.0F, .y = 0.0F });
            state.hasVelocity = true;
        }
        if ((mask & 0x2U) != 0U) {
            world.Set(entity, EcsChurnMass{ .value = 4.0F });
            state.hasMass = true;
        }
        if ((mask & 0x4U) != 0U) {
            world.Set(entity, EcsQueryMarker{ .value = static_cast<int>(mask) });
            state.hasMarker = true;
        }
        if ((mask & 0x8U) != 0U) {
            world.Set(entity, EcsChurnPayload{ .value = static_cast<int>(mask * 3U) });
            state.hasPayload = true;
        }
        alive.push_back(state);
    };

    for (int index = 0; index < 96; ++index) {
        createEntity(static_cast<float>(index), static_cast<std::uint32_t>(index) & 0xFU);
    }

    kb::ecs::Query<EcsPosition, EcsVelocity> movingQuery = world.CreateQuery<EcsPosition, EcsVelocity>();
    kb::ecs::Query<EcsPosition, EcsChurnMass> massQuery = world.CreateQuery<EcsPosition, EcsChurnMass>();

    for (int frame = 0; frame < 80; ++frame) {
        for (int operation = 0; operation < 64; ++operation) {
            const std::uint32_t value = NextStressValue(randomState);
            if (alive.empty() || (value % 11U) == 0U) {
                createEntity(static_cast<float>((value >> 8U) & 0x3FFU), (value >> 4U) & 0xFU);
                continue;
            }

            const std::size_t index = static_cast<std::size_t>(value % alive.size());
            StressEntityState& state = alive[index];
            switch ((value >> 16U) % 6U) {
            case 0:
                state.positionX += 1.0F;
                world.Set(state.entity, EcsPosition{ .x = state.positionX, .y = 0.0F });
                break;
            case 1:
                if (state.hasVelocity) {
                    world.Remove<EcsVelocity>(state.entity);
                    state.hasVelocity = false;
                } else {
                    world.Set(state.entity, EcsVelocity{ .x = 2.0F, .y = 0.0F });
                    state.hasVelocity = true;
                }
                break;
            case 2:
                if (state.hasMass) {
                    world.Remove<EcsChurnMass>(state.entity);
                    state.hasMass = false;
                } else {
                    world.Set(state.entity, EcsChurnMass{ .value = 4.0F });
                    state.hasMass = true;
                }
                break;
            case 3:
                if (state.hasMarker) {
                    world.Remove<EcsQueryMarker>(state.entity);
                    state.hasMarker = false;
                } else {
                    world.Set(state.entity, EcsQueryMarker{ .value = frame });
                    state.hasMarker = true;
                }
                break;
            case 4:
                if (state.hasPayload) {
                    world.Remove<EcsChurnPayload>(state.entity);
                    state.hasPayload = false;
                } else {
                    world.Set(state.entity, EcsChurnPayload{ .value = operation });
                    state.hasPayload = true;
                }
                break;
            default: {
                const kb::ecs::Entity destroyed = state.entity;
                world.DestroyEntity(destroyed);
                alive[index] = alive.back();
                alive.pop_back();
                kb::tests::Require(!world.IsAlive(destroyed), "ECS archetype churn destroy left an entity alive");
                break;
            }
            }
        }

        int expectedMoving = 0;
        int expectedMass = 0;
        float expectedMovingSum = 0.0F;
        float expectedMassSum = 0.0F;
        std::array<bool, 16U> occupiedMasks{};
        std::size_t expectedArchetypes = 0;
        for (const StressEntityState& state : alive) {
            kb::tests::Require(world.IsAlive(state.entity), "ECS archetype churn model contains a dead entity");
            kb::tests::Require(world.Has<EcsPosition>(state.entity), "ECS archetype churn entity lost required position");
            std::uint32_t mask = 0U;
            if (state.hasVelocity) {
                ++expectedMoving;
                expectedMovingSum += state.positionX + 2.0F;
                mask |= 0x1U;
                kb::tests::Require(world.Has<EcsVelocity>(state.entity), "ECS archetype churn lost velocity");
            }
            if (state.hasMass) {
                ++expectedMass;
                expectedMassSum += state.positionX + 4.0F;
                mask |= 0x2U;
                kb::tests::Require(world.Has<EcsChurnMass>(state.entity), "ECS archetype churn lost mass");
            }
            if (state.hasMarker) {
                mask |= 0x4U;
                kb::tests::Require(world.Has<EcsQueryMarker>(state.entity), "ECS archetype churn lost marker");
            }
            if (state.hasPayload) {
                mask |= 0x8U;
                kb::tests::Require(world.Has<EcsChurnPayload>(state.entity), "ECS archetype churn lost payload");
            }
            if (!occupiedMasks[mask]) {
                occupiedMasks[mask] = true;
                ++expectedArchetypes;
            }
        }

        EcsIterationCounters movingCounters;
        movingQuery.ForEachBatch(kb::ecs::QueryExecutionSettings{ .maxBatchSize = 5 }, &CountMovingBatches, &movingCounters);
        kb::tests::Require(movingCounters.visited == expectedMoving, "ECS archetype churn moving query returned an invalid count");
        kb::tests::Require(kb::tests::NearlyEqual(movingCounters.sumX, expectedMovingSum), "ECS archetype churn moving query returned invalid data");

        EcsIterationCounters massCounters;
        massQuery.ForEachBatch(kb::ecs::QueryExecutionSettings{ .maxBatchSize = 5 }, &CountMassBatches, &massCounters);
        kb::tests::Require(massCounters.visited == expectedMass, "ECS archetype churn mass query returned an invalid count");
        kb::tests::Require(kb::tests::NearlyEqual(massCounters.sumX, expectedMassSum), "ECS archetype churn mass query returned invalid data");

        const kb::ecs::NativeEcsStorageStats stats = world.NativeStorageStats();
        kb::tests::Require(stats.liveEntities == alive.size(), "ECS archetype churn native storage live count diverged from the model");
        kb::tests::Require(stats.archetypeCount >= expectedArchetypes, "ECS archetype churn did not expose expected archetype diversity");
    }
}

} // namespace

namespace kb::tests {

void RunEcsComponentApiTests() {
    RunTypedEcsComponentApiTest();
    RunTypedEcsComponentStoragePolicyTest();
    RunTypedEcsEntityAndComponentLifetimeValidationTest();
    RunTypedEcsBulkDestroyValidationTest();
    RunTypedEcsRandomStructuralStressTest();
    RunTypedEcsArchetypeChurnStressTest();
}

} // namespace kb::tests
