#include "EcsTestSuites.hpp"
#include "EcsTestTypes.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/Query.hpp"
#include "engine/ecs/World.hpp"

#include <cstdint>
#include <random>
#include <sstream>
#include <string>
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

} // namespace

namespace kb::tests {

void RunEcsSeededStressTests() {
    RunSeededStructuralStressTest();
}

} // namespace kb::tests
