#include "EcsTestSuites.hpp"
#include "EcsTestTypes.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/World.hpp"

#include <flecs.h>

#include <utility>

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

void RunComponentObserverLifetimeTest() {
    ComponentEventCounters counters;
    kb::ecs::World world;
    const auto componentId = world.RegisterComponent<EcsPosition>();
    const auto entity = world.CreateEntity();
    world.Set(entity, EcsPosition{ .x = 1.0F });
    kb::tests::Require(!world.MirrorsValueWrites(componentId), "Unobserved component must not mirror in-place writes");
    const auto first = world.ObserveComponent<EcsPosition>(kb::ecs::ComponentEventKind::Modified, &CountComponentEvents, &counters);
    const auto second = world.ObserveComponent<EcsPosition>(kb::ecs::ComponentEventKind::Added, &CountComponentEvents, &counters);
    const auto third = world.ObserveComponent<EcsPosition>(kb::ecs::ComponentEventKind::Removed, &CountComponentEvents, &counters);
    kb::tests::Require(first != 0U && second != 0U && third != 0U, "Observer lifetime registration failed");
    world.DestroyObserver(second);
    world.DestroyObserver(second);
    kb::tests::Require(world.MirrorsValueWrites(componentId), "Destroying one subscription must preserve the remaining receivers");
    world.TryGetMutable<EcsPosition>(entity)->x = 2.0F;
    world.MarkModified<EcsPosition>(entity);
    kb::tests::Require(counters.modified == 1 && counters.lastX == 2.0F, "Remaining modified receiver lost publication");
    world.DestroyObserver(first);
    kb::tests::Require(world.MirrorsValueWrites(componentId), "A remaining removed receiver must retain value publication");
    world.DestroyObserver(third);
    kb::tests::Require(!world.MirrorsValueWrites(componentId), "The last destroyed receiver must release value publication");
    const auto failed = world.ObserveComponent<EcsPosition>(kb::ecs::ComponentEventKind::Modified, nullptr, nullptr);
    kb::tests::Require(failed == 0U && !world.MirrorsValueWrites(componentId), "Failed observer registration must not retain publication");

    // The next first subscriber must synchronize values written while no one
    // observed this component, before yieldExisting invokes its callback.
    world.TryGetMutable<EcsPosition>(entity)->x = 42.0F;
    world.MarkModified<EcsPosition>(entity);
    const auto late = world.ObserveComponent<EcsPosition>(kb::ecs::ComponentEventKind::Modified, &CountComponentEvents, &counters, true);
    kb::tests::Require(late != 0U && counters.modified == 2 && counters.lastX == 42.0F,
        "Recreated observer must yield the current native component value");
    // A raw backend deletion must release a managed subscription too.
    ecs_delete(world.NativeHandle(), late);
    kb::tests::Require(!world.MirrorsValueWrites(componentId), "Raw deletion of a managed observer must release publication");
    world.DestroyObserver(late);
    kb::tests::Require(!world.MirrorsValueWrites(componentId), "Destroying an already removed observer must not change counts");
}

void RunComponentObserverMoveAndDeferredDeleteTest() {
    ComponentEventCounters counters;
    kb::ecs::World original;
    const auto componentId = original.RegisterComponent<EcsPosition>();
    const auto observer = original.ObserveComponent<EcsPosition>(kb::ecs::ComponentEventKind::Modified, &CountComponentEvents, &counters);
    kb::ecs::World moved{ std::move(original) };
    kb::tests::Require(moved.MirrorsValueWrites(componentId), "Moving a world must retain its observer publication");
    kb::ecs::World assigned;
    const auto discarded = assigned.ObserveComponent<EcsPosition>(kb::ecs::ComponentEventKind::Modified, &CountComponentEvents, &counters);
    kb::tests::Require(discarded != 0U, "Move-assignment target observer registration failed");
    assigned = std::move(moved);
    kb::tests::Require(assigned.MirrorsValueWrites(componentId), "Move assignment must retain the incoming receivers");
    ecs_defer_begin(assigned.NativeHandle());
    assigned.DestroyObserver(observer);
    kb::tests::Require(assigned.MirrorsValueWrites(componentId), "Deferred observer destruction must retain publication until the receiver is removed");
    ecs_defer_end(assigned.NativeHandle());
    kb::tests::Require(!assigned.MirrorsValueWrites(componentId), "Deferred observer destruction must release publication after merge");
}

struct ObserverYieldReentry {
    kb::ecs::World* world = nullptr;
    kb::ecs::ObserverId existing = 0U;
    kb::ecs::ObserverId nested = 0U;
    ComponentEventCounters counters;
};

void ReenterObserverRegistration(kb::ecs::Entity entity, kb::ecs::ComponentEventKind event, const EcsPosition* position, void* context) {
    auto& state = *static_cast<ObserverYieldReentry*>(context);
    CountComponentEvents(entity, event, position, &state.counters);
    if (state.nested == 0U) {
        state.nested = state.world->ObserveComponent<EcsPosition>(kb::ecs::ComponentEventKind::Removed, &CountComponentEvents, &state.counters);
        state.world->DestroyObserver(state.existing);
    }
}

void RunComponentObserverYieldReentryTest() {
    kb::ecs::World world;
    ObserverYieldReentry state{ .world = &world };
    const auto componentId = world.RegisterComponent<EcsPosition>();
    const auto entity = world.CreateEntity();
    world.Set(entity, EcsPosition{ .x = 7.0F });
    state.existing = world.ObserveComponent<EcsPosition>(kb::ecs::ComponentEventKind::Added, &CountComponentEvents, &state.counters);
    const auto yielding = world.ObserveComponent<EcsPosition>(kb::ecs::ComponentEventKind::Modified, &ReenterObserverRegistration, &state, true);
    kb::tests::Require(yielding != 0U && state.nested != 0U && state.counters.modified == 1,
        "Observer yield reentry must preserve both newly created subscriptions");
    world.DestroyObserver(yielding);
    kb::tests::Require(world.MirrorsValueWrites(componentId), "Nested observer must survive destruction of the yielding observer");
    world.DestroyObserver(state.nested);
    kb::tests::Require(!world.MirrorsValueWrites(componentId), "Yield reentry must balance the final subscription count");
}

void RunRawBackendObserverLifetimeTest() {
    kb::ecs::WorldConfig config;
    config.mirrorValueWritesOnlyForObservedComponents = false;
    kb::ecs::World world{ config };
    const auto componentId = world.RegisterComponent<EcsPosition>();
    const auto entity = world.CreateEntity();
    world.Set(entity, EcsPosition{});
    int rawCalls = 0;
    ecs_observer_desc_t desc{};
    desc.query.terms[0].id = componentId;
    desc.events[0] = EcsOnSet;
    desc.ctx = &rawCalls;
    desc.callback = [](ecs_iter_t* iterator) { *static_cast<int*>(iterator->ctx) += iterator->count; };
    const auto rawObserver = ecs_observer_init(world.NativeHandle(), &desc);
    ComponentEventCounters counters;
    const auto managed = world.ObserveComponent<EcsPosition>(kb::ecs::ComponentEventKind::Modified, &CountComponentEvents, &counters);
    kb::tests::Require(rawObserver != 0U && managed != 0U, "Mixed observer registration failed");
    world.DestroyObserver(managed);
    world.TryGetMutable<EcsPosition>(entity)->x = 11.0F;
    world.MarkModified<EcsPosition>(entity);
    kb::tests::Require(world.MirrorsValueWrites(componentId) && rawCalls == 1 && counters.modified == 0,
        "The last managed observer must not disable an authorized raw backend receiver");
    world.DestroyObserver(rawObserver);
}

struct SelfDestroyingObserver {
    kb::ecs::World* world = nullptr;
    kb::ecs::ObserverId observer = 0U;
    int calls = 0;
};

void DestroyCurrentObserver(kb::ecs::Entity, kb::ecs::ComponentEventKind, const EcsPosition*, void* context) {
    auto& state = *static_cast<SelfDestroyingObserver*>(context);
    ++state.calls;
    state.world->DestroyObserver(state.observer);
}

void RunComponentObserverSelfDestroyTest() {
    kb::ecs::World world;
    const auto first = world.CreateEntity();
    const auto second = world.CreateEntity();
    world.Set(first, EcsPosition{});
    world.Set(second, EcsPosition{});
    const auto componentId = world.Component<EcsPosition>();
    SelfDestroyingObserver state{ .world = &world };
    state.observer = world.ObserveComponent<EcsPosition>(kb::ecs::ComponentEventKind::Modified, &DestroyCurrentObserver, &state);
    kb::tests::Require(state.observer != 0U, "Self-destroying observer registration failed");
    world.TryGetMutable<EcsPosition>(first)->x = 7.0F;
    world.MarkModified<EcsPosition>(first);
    kb::tests::Require(state.calls == 1 && !world.MirrorsValueWrites(componentId),
        "A self-destroyed receiver must release publication after its callback returns");
    world.TryGetMutable<EcsPosition>(second)->x = 8.0F;
    world.MarkModified<EcsPosition>(second);
    kb::tests::Require(state.calls == 1, "A self-destroyed observer must not receive a later publication");
}

struct InheritedEventCounters {
    int calls = 0;
    int wrongValues = 0;
};

void CountInheritedEvents(kb::ecs::Entity, kb::ecs::ComponentEventKind, const EcsPosition* position, void* context) {
    auto& counters = *static_cast<InheritedEventCounters*>(context);
    ++counters.calls;
    if (position == nullptr || position->x != 42.0F || position->y != 17.0F) ++counters.wrongValues;
}

void RunComponentObserverInheritedYieldTest() {
    // A shared query field has one value for all matching children, rather than
    // an array of values. Sparse fields must instead be obtained per row.
    for (const bool sparse : { false, true }) {
        kb::ecs::WorldConfig config;
        config.mirrorValueWritesOnlyForObservedComponents = false;
        kb::ecs::World world{ config };
        const auto componentId = world.RegisterComponent<EcsPosition>();
        auto* backend = world.NativeHandle();
        if (sparse) ecs_add_id(backend, componentId, EcsSparse);
        ecs_add_pair(backend, componentId, EcsOnInstantiate, EcsInherit);
        const auto base = ecs_new(backend);
        const EcsPosition value{ .x = 42.0F, .y = 17.0F };
        ecs_set_id(backend, base, componentId, sizeof(value), &value);
        const auto first = ecs_new(backend);
        const auto second = ecs_new(backend);
        ecs_add_pair(backend, first, EcsIsA, base);
        ecs_add_pair(backend, second, EcsIsA, base);
        InheritedEventCounters counters;
        const auto observer = world.ObserveComponent<EcsPosition>(kb::ecs::ComponentEventKind::Modified, &CountInheritedEvents, &counters, true);
        kb::tests::Require(observer != 0U && counters.calls == 3 && counters.wrongValues == 0,
            "Observer yield must preserve shared and sparse inherited field values for every row");
        world.DestroyObserver(observer);
    }
}

} // namespace

namespace kb::tests {

void RunEcsEventTests() {
    RunComponentObserverTest();
    RunUnobservedValueWriteTest();
    RunComponentObserverLifetimeTest();
    RunComponentObserverMoveAndDeferredDeleteTest();
    RunComponentObserverYieldReentryTest();
    RunRawBackendObserverLifetimeTest();
    RunComponentObserverSelfDestroyTest();
    RunComponentObserverInheritedYieldTest();
}

} // namespace kb::tests
