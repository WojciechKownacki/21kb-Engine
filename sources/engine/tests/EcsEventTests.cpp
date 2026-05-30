#include "EcsTestSuites.hpp"
#include "EcsTestTypes.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/World.hpp"

namespace {

struct ComponentEventCounters {
    int added = 0;
    int modified = 0;
    int removed = 0;
    float lastX = 0.0F;
};

void CountComponentEvents(kb::ecs::Entity entity, kb::ecs::ComponentEventKind event, const EcsPosition* position, void* context) {
    kb::tests::Require(entity.IsValid(), "ECS component observer received invalid entity");

    auto* counters = static_cast<ComponentEventCounters*>(context);
    switch (event) {
    case kb::ecs::ComponentEventKind::Added:
        ++counters->added;
        break;
    case kb::ecs::ComponentEventKind::Modified:
        ++counters->modified;
        break;
    case kb::ecs::ComponentEventKind::Removed:
        ++counters->removed;
        break;
    }

    if (position != nullptr) {
        counters->lastX = position->x;
    }
}

void RunComponentObserverTest() {
    kb::ecs::World world;
    ComponentEventCounters counters;

    const kb::ecs::ObserverId addedObserver = world.ObserveComponent<EcsPosition>(kb::ecs::ComponentEventKind::Added, &CountComponentEvents, &counters);
    const kb::ecs::ObserverId modifiedObserver = world.ObserveComponent<EcsPosition>(kb::ecs::ComponentEventKind::Modified, &CountComponentEvents, &counters);
    const kb::ecs::ObserverId removedObserver = world.ObserveComponent<EcsPosition>(kb::ecs::ComponentEventKind::Removed, &CountComponentEvents, &counters);
    kb::tests::Require(addedObserver != 0 && modifiedObserver != 0 && removedObserver != 0, "ECS component observer registration failed");

    const kb::ecs::Entity entity = world.CreateEntity("Observed");
    world.Set(entity, EcsPosition{ .x = 7.0F, .y = 1.0F });
    kb::tests::Require(counters.added == 1, "ECS component add observer was not called");
    kb::tests::Require(counters.modified == 1, "ECS component set observer was not called");
    kb::tests::Require(kb::tests::NearlyEqual(counters.lastX, 7.0F), "ECS component observer saw invalid component data");

    world.MarkModified<EcsPosition>(entity);
    kb::tests::Require(counters.modified == 2, "ECS component modified observer was not called");

    world.Remove<EcsPosition>(entity);
    kb::tests::Require(counters.removed == 1, "ECS component remove observer was not called");

    world.DestroyObserver(modifiedObserver);
    world.Set(entity, EcsPosition{ .x = 9.0F, .y = 2.0F });
    kb::tests::Require(counters.modified == 2, "Destroyed ECS component observer was called");
}

} // namespace

namespace kb::tests {

void RunEcsEventTests() {
    RunComponentObserverTest();
}

} // namespace kb::tests
