#include "engine/ecs/World.hpp"

#include <flecs.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using kb::ecs::ComponentEventKind;
using kb::ecs::Entity;
using kb::ecs::GeneratedEntityIndex;
using kb::ecs::World;

struct Value { std::uint32_t token = 0U; };
struct Other { std::uint32_t token = 0U; };
struct AddedLater { std::uint32_t token = 0U; };

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Event {
    Entity entity{};
    ComponentEventKind kind = ComponentEventKind::Added;
    std::uint32_t token = 0U;
    bool nativeAlive = false;
};

struct Recorder {
    World* world = nullptr;
    std::vector<Event> events;
    bool missingValue = false;

    explicit Recorder(World* owner = nullptr) : world(owner) { events.reserve(4096U); }

    void Record(Entity entity, ComponentEventKind kind, const Value* value) {
        if (value == nullptr) missingValue = true;
        events.push_back(Event{ entity, kind, value != nullptr && kind != ComponentEventKind::Added ? value->token : 0U,
            world != nullptr && world->IsAlive(entity) });
    }
};

void RecordValue(Entity entity, ComponentEventKind kind, const Value* value, void* context) {
    static_cast<Recorder*>(context)->Record(entity, kind, value);
}

void CheckEvents(const Recorder& recorder, ComponentEventKind kind,
    std::span<const Entity> entities, std::span<const Value> values, bool dead) {
    Require(!recorder.missingValue, "Observer received a null component value");
    std::vector<std::uint32_t> visits(entities.size(), 0U);
    std::size_t matchingCount = 0U;
    for (const Event& event : recorder.events) {
        if (event.kind != kind) continue;
        ++matchingCount;
        const auto found = std::find(entities.begin(), entities.end(), event.entity);
        Require(found != entities.end(), "Observer emitted an unknown or stale full entity ID");
        const std::size_t index = static_cast<std::size_t>(found - entities.begin());
        ++visits[index];
        if (kind != ComponentEventKind::Added) {
            Require(event.token == values[index].token, "Observer component payload belongs to a different lifetime");
        }
        if (dead) Require(!event.nativeAlive, "Destroyed observer owner remained natively alive");
    }
    Require(matchingCount == entities.size(), "Observer event count differs from the entity ledger");
    Require(std::all_of(visits.begin(), visits.end(), [](std::uint32_t count) { return count == 1U; }),
        "Observer full entity coverage was skipped or repeated");
}

Entity Reused(World& world, std::uint32_t token) {
    const Entity old = world.CreateEntity();
    world.DestroyEntity(old);
    const Entity entity = world.CreateEntity();
    Require(GeneratedEntityIndex(old) == GeneratedEntityIndex(entity) && old != entity,
        "Fixture did not reuse a native slot with a changed full generation");
    world.Set(entity, Value{ token });
    return entity;
}

std::vector<Entity> Bulk(World& world, std::span<const Value> values, bool nativeOnly = false) {
    const std::array views{ World::MakeBulkComponentView<Value>(values) };
    return nativeOnly ? world.CreateEntitiesNativeOnly(values.size(), views)
        : world.CreateEntities(values.size(), views);
}

void RunSynchronous() {
    for (const bool catalog : { false, true }) {
        kb::ecs::WorldConfig config;
        config.trackEntityCatalog = catalog;
        Recorder recorder;
        World world{ config };
        recorder.world = &world;
        const Entity first = Reused(world, 17U);
        const auto added = world.ObserveComponent<Value>(ComponentEventKind::Added, &RecordValue, &recorder);
        const auto modified = world.ObserveComponent<Value>(ComponentEventKind::Modified, &RecordValue, &recorder);
        const auto removed = world.ObserveComponent<Value>(ComponentEventKind::Removed, &RecordValue, &recorder);
        Require(added != 0U && modified != 0U && removed != 0U, "Identity observer registration failed");
        world.Set(first, Value{ 19U });
        world.DestroyEntity(first);
        const std::array firstIds{ first };
        const std::array firstValues{ Value{ 19U } };
        CheckEvents(recorder, ComponentEventKind::Modified, firstIds, firstValues, false);
        CheckEvents(recorder, ComponentEventKind::Removed, firstIds, firstValues, true);
        recorder.events.clear();

        const Entity next = world.CreateEntity();
        Require(GeneratedEntityIndex(next) == GeneratedEntityIndex(first) && next != first,
            "Synchronous second-generation fixture missed reuse");
        world.Set(next, Value{ 29U });
        const std::array nextIds{ next };
        const std::array nextValues{ Value{ 29U } };
        CheckEvents(recorder, ComponentEventKind::Added, nextIds, nextValues, false);
        CheckEvents(recorder, ComponentEventKind::Modified, nextIds, nextValues, false);
        world.DestroyObserver(added);
        world.DestroyObserver(modified);
        world.DestroyObserver(removed);
    }
}

void RunBulk() {
    Recorder recorder;
    World world;
    recorder.world = &world;
    std::vector<Value> values(257U);
    for (std::size_t index = 0U; index < values.size(); ++index) values[index].token = static_cast<std::uint32_t>(index + 100U);
    const auto old = Bulk(world, values);
    world.DestroyEntities(old);
    const auto added = world.ObserveComponent<Value>(ComponentEventKind::Added, &RecordValue, &recorder);
    const auto modified = world.ObserveComponent<Value>(ComponentEventKind::Modified, &RecordValue, &recorder);
    const auto removed = world.ObserveComponent<Value>(ComponentEventKind::Removed, &RecordValue, &recorder);
    const auto entities = Bulk(world, values);
    for (std::size_t index = 0U; index < entities.size(); ++index) {
        Require(entities[index] != old[index] && GeneratedEntityIndex(entities[index]) == GeneratedEntityIndex(old[index]),
            "Bulk native generation/ordering fixture was not established");
    }
    CheckEvents(recorder, ComponentEventKind::Added, entities, values, false);
    CheckEvents(recorder, ComponentEventKind::Modified, entities, values, false);
    world.DestroyEntities(entities);
    CheckEvents(recorder, ComponentEventKind::Removed, entities, values, true);
    world.DestroyObserver(added);
    world.DestroyObserver(modified);
    world.DestroyObserver(removed);
}

void RunDeferredExpiry() {
    Recorder recorder;
    World world;
    recorder.world = &world;
    const Entity entity = Reused(world, 31U);
    const auto component = world.Component<Value>();
    const auto removed = world.ObserveComponent<Value>(ComponentEventKind::Removed, &RecordValue, &recorder);
    const auto modified = world.ObserveComponent<Value>(ComponentEventKind::Modified, &RecordValue, &recorder);
    auto* backend = world.NativeHandle();
    ecs_defer_begin(backend);
    world.DestroyEntity(entity);
    Require(!world.IsAlive(entity) && recorder.events.empty(), "Deferred delete did not retain the pending backend lifetime");
    const Entity during = world.CreateEntity();
    Require(GeneratedEntityIndex(during) != GeneratedEntityIndex(entity), "Pending backend owner was reused before merge");
    world.Set(during, Other{ 73U });
    ecs_defer_end(backend);
    const std::array ids{ entity };
    const std::array values{ Value{ 31U } };
    CheckEvents(recorder, ComponentEventKind::Removed, ids, values, true);
    recorder.events.clear();

    // Force ordinary raw generation-zero revival after the managed deferred
    // delete completed. The expired logical generation must not be retained.
    const ecs_entity_t raw = ecs_strip_generation(entity.Id());
    Require(raw != entity.Id(), "Expiry fixture requires a nonzero logical generation");
    ecs_make_alive(backend, raw);
    const Value rawValue{ 91U };
    ecs_set_id(backend, raw, component, sizeof(Value), &rawValue);
    const std::array rawIds{ Entity{ raw } };
    const std::array rawValues{ rawValue };
    CheckEvents(recorder, ComponentEventKind::Modified, rawIds, rawValues, false);
    recorder.events.clear();
    ecs_delete(backend, raw);
    CheckEvents(recorder, ComponentEventKind::Removed, rawIds, rawValues, true);
    world.DestroyObserver(removed);
    world.DestroyObserver(modified);
}

struct RemoveReentry {
    World* world = nullptr;
    std::vector<Entity> originals;
    Recorder recorder;
    Entity replacement{};
    bool entered = false;
    bool reusedPendingIndex = false;
};

void RemoveAndCreate(Entity entity, ComponentEventKind kind, const Value* value, void* context) {
    auto& state = *static_cast<RemoveReentry*>(context);
    state.recorder.Record(entity, kind, value);
    if (!state.entered && !state.world->ShouldQuit()) {
        state.entered = true;
        state.world->DestroyEntities(state.originals);
        state.replacement = state.world->CreateEntity();
        for (Entity original : state.originals) {
            if (GeneratedEntityIndex(original) == GeneratedEntityIndex(state.replacement)) state.reusedPendingIndex = true;
        }
        state.world->Set(state.replacement, Value{ 999U });
    }
}

void RunRemovedBatchReentry() {
    RemoveReentry state;
    World world;
    std::vector<Value> values(65U);
    for (std::size_t index = 0U; index < values.size(); ++index) values[index].token = static_cast<std::uint32_t>(index + 200U);
    const auto warm = Bulk(world, values);
    world.DestroyEntities(warm);
    state.world = &world;
    state.recorder.world = &world;
    state.originals = Bulk(world, values);
    const auto observer = world.ObserveComponent<Value>(ComponentEventKind::Removed, &RemoveAndCreate, &state);
    Require(observer != 0U, "Removed reentry observer registration failed");
    // Flecs deletes a whole matching table, so this exercises a multi-row
    // Removed iterator. The first visitor destroys native owners and creates
    // another owner while the rest of this original iterator is still active.
    ecs_delete_with(world.NativeHandle(), world.Component<Value>());
    Require(state.entered && !state.reusedPendingIndex, "Removed reentry reused an owner before backend removal finished");
    CheckEvents(state.recorder, ComponentEventKind::Removed, state.originals, values, false);
    Require(world.IsAlive(state.replacement) && world.TryGet<Value>(state.replacement)->token == 999U,
        "Removed reentry lost the new owner or its value");
    world.Set(state.replacement, Value{ 1001U });
    world.DestroyObserver(observer);
}

struct YieldReentry {
    World* world = nullptr;
    std::vector<Entity> originals;
    Recorder yielded;
    Entity replacement{};
    bool entered = false;
    bool reusedPendingIndex = false;
};

void YieldAndCreate(Entity entity, ComponentEventKind kind, const Value* value, void* context) {
    auto& state = *static_cast<YieldReentry*>(context);
    state.yielded.Record(entity, kind, value);
    if (!state.entered && !state.world->ShouldQuit()) {
        state.entered = true;
        state.world->DestroyEntities(state.originals);
        state.replacement = state.world->CreateEntity();
        for (Entity original : state.originals) {
            if (GeneratedEntityIndex(original) == GeneratedEntityIndex(state.replacement)) state.reusedPendingIndex = true;
        }
        state.world->Set(state.replacement, Value{ 999U });
    }
}

void RunYieldReentry() {
    YieldReentry state;
    Recorder removed;
    World world;
    removed.world = &world;
    std::vector<Value> values(70U);
    for (std::size_t index = 0U; index < values.size(); ++index) values[index].token = static_cast<std::uint32_t>(index + 400U);
    const auto warm = Bulk(world, values);
    world.DestroyEntities(warm);
    state.world = &world;
    state.yielded.world = &world;
    state.originals = Bulk(world, values);
    for (std::size_t index = 0U; index < state.originals.size(); index += 2U) {
        world.Set(state.originals[index], Other{ static_cast<std::uint32_t>(index) });
    }
    const auto removal = world.ObserveComponent<Value>(ComponentEventKind::Removed, &RecordValue, &removed);
    const auto observer = world.ObserveComponent<Value>(ComponentEventKind::Modified, &YieldAndCreate, &state, true);
    Require(observer != 0U && state.entered && !state.reusedPendingIndex, "Yield reentry changed a pending backend owner");
    std::vector<Entity> expected = state.originals;
    expected.push_back(state.replacement);
    auto expectedValues = values;
    expectedValues.push_back(Value{ 999U });
    CheckEvents(state.yielded, ComponentEventKind::Modified, expected, expectedValues, false);
    CheckEvents(removed, ComponentEventKind::Removed, state.originals, values, true);
    Require(world.IsAlive(state.replacement), "Yield reentry lost its replacement owner");
    world.DestroyObserver(observer);
    world.DestroyObserver(removal);
}

void RunMovedDeferred() {
    Recorder recorder;
    World original;
    recorder.world = &original;
    const Entity entity = Reused(original, 57U);
    const auto removed = original.ObserveComponent<Value>(ComponentEventKind::Removed, &RecordValue, &recorder);
    const auto modified = original.ObserveComponent<Value>(ComponentEventKind::Modified, &RecordValue, &recorder);
    ecs_defer_begin(original.NativeHandle());
    original.DestroyEntity(entity);
    World moved{ std::move(original) };
    recorder.world = &moved;
    ecs_defer_end(moved.NativeHandle());
    const std::array ids{ entity };
    const std::array values{ Value{ 57U } };
    CheckEvents(recorder, ComponentEventKind::Removed, ids, values, true);
    World assigned;
    assigned = std::move(moved);
    recorder.world = &assigned;
    recorder.events.clear();
    const Entity next = assigned.CreateEntity();
    assigned.Set(next, Value{ 61U });
    const std::array nextIds{ next };
    const std::array nextValues{ Value{ 61U } };
    CheckEvents(recorder, ComponentEventKind::Modified, nextIds, nextValues, false);
    assigned.DestroyObserver(removed);
    assigned.DestroyObserver(modified);
}

struct RawRecorder { std::vector<Entity::IdType> ids; };

void RunRawGenerationAndNativeOnly() {
    {
        kb::ecs::WorldConfig config;
        config.mirrorValueWritesOnlyForObservedComponents = false;
        Recorder recorder;
        RawRecorder raw;
        World world{ config };
        recorder.world = &world;
        const Entity entity = Reused(world, 81U);
        const auto component = world.Component<Value>();
        const auto managed = world.ObserveComponent<Value>(ComponentEventKind::Modified, &RecordValue, &recorder);
        raw.ids.reserve(8U);
        ecs_observer_desc_t observer{};
        observer.query.terms[0].id = component;
        observer.events[0] = EcsOnSet;
        observer.ctx = &raw;
        observer.callback = [](ecs_iter_t* iterator) {
            auto& state = *static_cast<RawRecorder*>(iterator->ctx);
            for (int row = 0; row < iterator->count; ++row) state.ids.push_back(iterator->entities[row]);
        };
        const auto rawObserver = ecs_observer_init(world.NativeHandle(), &observer);
        Require(managed != 0U && rawObserver != 0U, "Mixed raw and managed observer registration failed");
        world.DestroyEntity(entity);
        const ecs_entity_t replacement = ecs_new(world.NativeHandle());
        Require(ecs_strip_generation(replacement) == ecs_strip_generation(entity.Id())
            && replacement != ecs_strip_generation(replacement), "Raw generation fixture missed backend reuse");
        const Value rawValue{ 83U };
        ecs_set_id(world.NativeHandle(), replacement, component, sizeof(Value), &rawValue);
        const std::array ids{ Entity{ replacement } };
        const std::array values{ rawValue };
        CheckEvents(recorder, ComponentEventKind::Modified, ids, values, false);
        Require(raw.ids.size() == 1U && raw.ids.front() == replacement, "Raw observer backend ID contract changed");
        world.DestroyObserver(managed);
        world.DestroyObserver(rawObserver);
    }
    for (const bool mirror : { false, true }) {
        kb::ecs::WorldConfig config;
        config.mirrorEntitiesToBackend = mirror;
        config.mirrorNativeComponentChangesToBackend = mirror;
        Recorder recorder;
        World world{ config };
        recorder.world = &world;
        const Entity mirrored = mirror ? world.CreateEntity() : Entity{};
        const std::array values{ Value{ 111U }, Value{ 112U } };
        const auto native = Bulk(world, values, true);
        const auto observer = world.ObserveComponent<Value>(ComponentEventKind::Modified, &RecordValue, &recorder, true);
        Require(observer != 0U && recorder.events.empty(), "Native-only values unexpectedly acquired a backend binding");
        for (Entity entity : native) {
            Require(ecs_get_alive(world.NativeHandle(), ecs_strip_generation(entity.Id())) == 0U,
                "Managed observer ID allocation occupied a live native-only index");
        }
        const ecs_entity_t raw = ecs_get_max_id(world.NativeHandle()) + 50U;
        ecs_make_alive(world.NativeHandle(), raw);
        const Value rawValue{ 121U };
        ecs_set_id(world.NativeHandle(), raw, world.Component<Value>(), sizeof(Value), &rawValue);
        const std::array ids{ Entity{ raw } };
        const std::array rawValues{ rawValue };
        CheckEvents(recorder, ComponentEventKind::Modified, ids, rawValues, false);
        world.DestroyObserver(observer);
        if (mirrored.IsValid()) world.DestroyEntity(mirrored);
    }
}

void RunTeardown() {
    Recorder recorder;
    Entity entity{};
    {
        World world;
        recorder.world = &world;
        entity = Reused(world, 151U);
        const auto observer = world.ObserveComponent<Value>(ComponentEventKind::Removed, &RecordValue, &recorder);
        Require(observer != 0U, "Teardown observer registration failed");
    }
    recorder.world = nullptr;
    const std::array ids{ entity };
    const std::array values{ Value{ 151U } };
    CheckEvents(recorder, ComponentEventKind::Removed, ids, values, false);
}

struct Test { const char* name; void (*run)(); };

} // namespace

int main(int argc, char** argv) {
    const std::array tests{
        Test{ "synchronous", &RunSynchronous },
        Test{ "bulk", &RunBulk },
        Test{ "deferred-expiry", &RunDeferredExpiry },
        Test{ "removed-batch-reentry", &RunRemovedBatchReentry },
        Test{ "yield-reentry", &RunYieldReentry },
        Test{ "moved-deferred", &RunMovedDeferred },
        Test{ "raw-native-only", &RunRawGenerationAndNativeOnly },
        Test{ "teardown", &RunTeardown },
    };
    try {
        bool selected = false;
        for (const Test& test : tests) {
            if (argc > 1 && std::string_view{ argv[1] } != test.name) continue;
            selected = true;
            std::printf("START %s\n", test.name);
            std::fflush(stdout);
            test.run();
            std::printf("PASS %s\n", test.name);
            std::fflush(stdout);
        }
        Require(selected, "Unknown identity test selector");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL observer identity: %s\n", error.what());
        return 1;
    }
}
