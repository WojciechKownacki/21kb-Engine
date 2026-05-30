#include "EcsTestTypes.hpp"
#include "EcsTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/World.hpp"

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

} // namespace

namespace kb::tests {

void RunEcsComponentApiTests() {
    RunTypedEcsComponentApiTest();
}

} // namespace kb::tests
