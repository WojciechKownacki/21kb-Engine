#include "EcsTestSuites.hpp"
#include "EcsTestTypes.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/Query.hpp"
#include "engine/ecs/World.hpp"

#include <algorithm>
#include <cstdint>
#include <random>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

constexpr std::uint64_t kEcsStressSeed = 0x21CB20260616ULL;

struct StressEntityState {
    kb::ecs::Entity entity;
    bool hasPosition = false;
    bool hasVelocity = false;
    float positionX = 0.0F;
    float velocityX = 0.0F;
};

void RequireStress(bool condition, const char* message) {
    if (condition) {
        return;
    }

    std::ostringstream output;
    output << message << " seed=0x" << std::hex << kEcsStressSeed;
    const std::string fullMessage = output.str();
    kb::tests::Require(false, fullMessage.c_str());
}

void CountStressMovingEntities(kb::ecs::Entity entity, const EcsPosition& position, const EcsVelocity& velocity, void* context) {
    static_cast<void>(entity);
    auto* counters = static_cast<EcsIterationCounters*>(context);
    ++counters->visited;
    counters->sumX += position.x + velocity.x;
}

void RunSeededStructuralStressTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 16,
    });
    std::mt19937_64 random{kEcsStressSeed};
    std::vector<StressEntityState> liveEntities;
    liveEntities.reserve(512);

    for (int frame = 0; frame < 128; ++frame) {
        for (int operation = 0; operation < 64; ++operation) {
            const std::uint64_t value = random();
            const std::uint64_t choice = value % 6U;
            if (choice == 0U || liveEntities.empty()) {
                const float positionX = static_cast<float>((value >> 8U) & 0x3FFU);
                const kb::ecs::Entity entity = world.CreateEntity();
                world.Set(entity, EcsPosition{ .x = positionX, .y = 0.0F });
                liveEntities.push_back(StressEntityState{
                    .entity = entity,
                    .hasPosition = true,
                    .positionX = positionX,
                });
                continue;
            }

            const std::size_t index = static_cast<std::size_t>(value % liveEntities.size());
            StressEntityState& state = liveEntities[index];
            if (choice == 1U) {
                const float velocityX = 1.0F + static_cast<float>((value >> 16U) & 0x1FU);
                world.Set(state.entity, EcsVelocity{ .x = velocityX, .y = 0.0F });
                state.hasVelocity = true;
                state.velocityX = velocityX;
            } else if (choice == 2U && state.hasVelocity) {
                world.Remove<EcsVelocity>(state.entity);
                state.hasVelocity = false;
                state.velocityX = 0.0F;
            } else if (choice == 3U && state.hasPosition) {
                state.positionX += 1.0F;
                world.Set(state.entity, EcsPosition{ .x = state.positionX, .y = 0.0F });
            } else if (choice == 4U && !state.hasPosition) {
                state.positionX = static_cast<float>((value >> 24U) & 0x3FFU);
                world.Set(state.entity, EcsPosition{ .x = state.positionX, .y = 0.0F });
                state.hasPosition = true;
            } else if (choice == 5U && state.hasPosition && !state.hasVelocity) {
                world.Remove<EcsPosition>(state.entity);
                state.hasPosition = false;
                state.positionX = 0.0F;
            } else {
                const kb::ecs::Entity destroyed = state.entity;
                world.DestroyEntity(destroyed);
                liveEntities[index] = liveEntities.back();
                liveEntities.pop_back();
                RequireStress(!world.IsAlive(destroyed), "ECS seeded stress destroy left an entity alive");
            }
        }

        int expectedMoving = 0;
        float expectedSum = 0.0F;
        for (const StressEntityState& state : liveEntities) {
            RequireStress(world.IsAlive(state.entity), "ECS seeded stress model retained a dead entity");
            RequireStress(world.Has<EcsPosition>(state.entity) == state.hasPosition, "ECS seeded stress position state diverged");
            RequireStress(world.Has<EcsVelocity>(state.entity) == state.hasVelocity, "ECS seeded stress velocity state diverged");
            if (state.hasPosition && state.hasVelocity) {
                ++expectedMoving;
                expectedSum += state.positionX + state.velocityX;
            }
        }

        EcsIterationCounters counters;
        kb::ecs::Query<EcsPosition, EcsVelocity> movingQuery = world.CreateQuery<EcsPosition, EcsVelocity>();
        movingQuery.ForEach(&CountStressMovingEntities, &counters);
        RequireStress(counters.visited == expectedMoving, "ECS seeded stress query count diverged");
        RequireStress(kb::tests::NearlyEqual(counters.sumX, expectedSum), "ECS seeded stress query data diverged");
    }
}

// LIB-074: Deterministic-order iteration is required to be safe to collect
// into a plain (unsynchronized) vector without a data race — QueryState.cpp
// forces serial execution whenever iterationOrder==Deterministic
// (IsDeterministicExecution gates the parallel path off), the same
// established idiom EcsQueryTests.cpp's CollectMovingSnapshot already
// relies on. Collects entity ids in VISITATION order (deliberately not
// sorted afterward, unlike CollectMovingSnapshot) — the whole point here is
// proving the order itself repeats, not just that the same set of ids
// comes back.
std::vector<std::uint64_t> CollectDeterministicEntityOrder(kb::ecs::Query<EcsPosition>& query) {
    std::vector<std::uint64_t> order;
    query.ForEachBatchKernel(kb::ecs::QueryExecutionSettings{
                                 .iterationOrder = kb::ecs::QueryIterationOrder::Deterministic,
                             },
                             [&order](const kb::ecs::QueryBatch<EcsPosition>& batch) {
                                 for (std::size_t index = 0; index < batch.Count(); ++index) {
                                     order.push_back(batch.EntityAt(index).Id());
                                 }
                             });
    return order;
}

// LIB-074: 10k-entity create/destroy churn — proves no handle leak (a
// destroyed entity never reports alive again; NativeStorageStats().
// liveEntities exactly tracks the real surviving population at every
// checkpoint, including back to 0 after a full churn cycle; no packed
// entity id, which encodes a generation bumped on every destroy
// (NativeArchetypeStorage.cpp's PackEntity/EntityGeneration), is EVER
// reissued across the whole run even after heavy index-slot reuse) and no
// unstable ordering (Query<>'s Deterministic iteration order over the same
// surviving population is byte-for-byte identical across two consecutive
// passes — StorageOrder is explicitly allowed to vary after a swap-remove
// churn, Deterministic is not, per QueryIterationOrder's own contract).
void RunEntityCreateDestroy10kStabilityTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 16,
    });
    std::mt19937_64 random{ kEcsStressSeed ^ 0xE10CDE57ULL };
    std::unordered_set<std::uint64_t> everIssuedIds;
    everIssuedIds.reserve(40'000);

    constexpr std::size_t kEntityCount = 10'000U;
    constexpr int kChurnCycles = 3;

    for (int cycle = 0; cycle < kChurnCycles; ++cycle) {
        std::vector<kb::ecs::Entity> entities;
        entities.reserve(kEntityCount);
        for (std::size_t index = 0; index < kEntityCount; ++index) {
            const kb::ecs::Entity entity = world.CreateEntity();
            world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 0.0F });
            RequireStress(everIssuedIds.insert(entity.Id()).second,
                "ECS 10k create/destroy stability test issued the same packed entity id twice — a leaked/reused handle that was never given a fresh generation");
            entities.push_back(entity);
        }
        RequireStress(world.NativeStorageStats().liveEntities == kEntityCount,
            "ECS 10k create/destroy stability test's live count did not match the number of entities just created");

        // Destroy order deliberately does NOT match creation order — a
        // real game churns entities in whatever order gameplay logic
        // decides, not FIFO. Half survive, so the ordering check below has
        // a real, non-trivial population to iterate, not a vacuous empty
        // query.
        std::shuffle(entities.begin(), entities.end(), random);
        const std::size_t destroyCount = entities.size() / 2U;
        for (std::size_t index = 0; index < destroyCount; ++index) {
            world.DestroyEntity(entities[index]);
        }
        const std::vector<kb::ecs::Entity> survivors(entities.begin() + static_cast<std::ptrdiff_t>(destroyCount), entities.end());

        for (std::size_t index = 0; index < destroyCount; ++index) {
            RequireStress(!world.IsAlive(entities[index]), "ECS 10k create/destroy stability test left a destroyed entity reporting alive (handle leak)");
        }
        for (const kb::ecs::Entity alive : survivors) {
            RequireStress(world.IsAlive(alive), "ECS 10k create/destroy stability test lost track of a surviving entity it never destroyed");
        }
        RequireStress(world.NativeStorageStats().liveEntities == survivors.size(),
            "ECS 10k create/destroy stability test's live count diverged from the real surviving population after a partial destroy churn");

        kb::ecs::Query<EcsPosition> survivorQuery = world.CreateQuery<EcsPosition>();
        const std::vector<std::uint64_t> firstOrder = CollectDeterministicEntityOrder(survivorQuery);
        const std::vector<std::uint64_t> secondOrder = CollectDeterministicEntityOrder(survivorQuery);
        RequireStress(firstOrder.size() == survivors.size(), "ECS 10k create/destroy stability test's Deterministic query did not visit exactly the surviving population");
        RequireStress(firstOrder == secondOrder,
            "ECS 10k create/destroy stability test's Deterministic query iteration order was NOT stable across two consecutive passes over the same surviving population");

        for (const kb::ecs::Entity alive : survivors) {
            world.DestroyEntity(alive);
        }
        RequireStress(world.NativeStorageStats().liveEntities == 0U,
            "ECS 10k create/destroy stability test did not return to zero live entities after a full churn cycle (handle/storage leak)");
    }
}

} // namespace

namespace kb::tests {

void RunEcsSeededStressTests() {
    RunSeededStructuralStressTest();
    RunEntityCreateDestroy10kStabilityTest();
}

} // namespace kb::tests
