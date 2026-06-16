#include "EcsTestTypes.hpp"
#include "EcsTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/World.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

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

    world.ForEachMutable<EcsPosition>(&ApplyVelocity, &world);
    position = world.TryGet<EcsPosition>(entity);
    kb::tests::Require(position != nullptr && kb::tests::NearlyEqual(position->x, 9.0F) && kb::tests::NearlyEqual(position->y, 2.0F), "Typed ECS mutable iteration did not update component data");

    EcsIterationCounters queryCounters;
    world.ForEach<EcsPosition, EcsVelocity>(&CountMovingPositions, &queryCounters);
    kb::tests::Require(queryCounters.visited == 1, "Typed ECS two-component query did not visit matching entity");
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

void RunTypedEcsEntityAndComponentLifetimeValidationTest() {
    kb::ecs::World world;
    const kb::ecs::Entity entity = world.CreateEntity("Lifetime");
    world.Set(entity, EcsPosition{ .x = 1.0F, .y = 2.0F });
    world.Set(entity, EcsVelocity{ .x = 3.0F, .y = 4.0F });

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

    bool invalidDestroyRejected = false;
    try {
        world.DestroyEntity(kb::ecs::Entity{});
    } catch (const std::invalid_argument&) {
        invalidDestroyRejected = true;
    }
    kb::tests::Require(invalidDestroyRejected, "ECS entity generation validation accepted an invalid destroy handle");
}

struct StressEntityState {
    kb::ecs::Entity entity;
    bool hasVelocity = false;
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

} // namespace

namespace kb::tests {

void RunEcsComponentApiTests() {
    RunTypedEcsComponentApiTest();
    RunTypedEcsEntityAndComponentLifetimeValidationTest();
    RunTypedEcsRandomStructuralStressTest();
}

} // namespace kb::tests
