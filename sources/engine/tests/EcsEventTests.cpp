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
    ComponentEventCounters counters;
    kb::ecs::World world;

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

// Value writes to an existing component are published to the backend world only once something observes the component:
// writes made before the first observer must still be what that observer sees.
void RunUnobservedValueWriteTest() {
    ComponentEventCounters counters;
    kb::ecs::World world;
    const kb::ecs::Entity first = world.CreateEntity("First");
    const kb::ecs::Entity second = world.CreateEntity("Second");
    world.Set(first, EcsPosition{ .x = 1.0F });
    world.Set(second, EcsPosition{ .x = 2.0F });
    for (float x = 10.0F; x < 13.0F; x += 1.0F) {
        world.TryGetMutable<EcsPosition>(first)->x = x;
        world.MarkModified<EcsPosition>(first);
    }
    world.TryGetMutable<EcsPosition>(second)->x = 20.0F; // never marked: only the native value changes
    const kb::ecs::ObserverId observer = world.ObserveComponent<EcsPosition>(kb::ecs::ComponentEventKind::Modified, &CountComponentEvents, &counters);
    kb::tests::Require(observer != 0, "ECS late observer registration failed");
    kb::tests::Require(counters.modified == 0, "Registering an observer must not report a change");
    world.MarkModified<EcsPosition>(first);
    kb::tests::Require(counters.modified == 1 && kb::tests::NearlyEqual(counters.lastX, 12.0F),
        "The first observer must see the value written before it was registered");
    world.MarkModified<EcsPosition>(second);
    kb::tests::Require(counters.modified == 2 && kb::tests::NearlyEqual(counters.lastX, 20.0F),
        "The observer must see a value written without a notification");
}

} // namespace

namespace kb::tests {

void RunEcsEventTests() {
    RunComponentObserverTest();
    RunUnobservedValueWriteTest();
}

} // namespace kb::tests
