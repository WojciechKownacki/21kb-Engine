#include "engine/ecs/World.hpp"

#include <flecs.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#if defined(KB_NATIVE_POLICY_FAULT_INJECTION)
#include <malloc.h>

namespace allocation_fault {
std::atomic<std::int64_t> remaining{ -1 };
std::atomic<std::size_t> failureBytes{ 0U };
std::atomic<bool> injected{ false };

void BeforeAllocate(std::size_t bytes) {
    auto count = remaining.load(std::memory_order_relaxed);
    while (count >= 0) {
        if (count == 0) {
            if (remaining.compare_exchange_weak(count, -1, std::memory_order_relaxed)) {
                failureBytes.store(bytes, std::memory_order_relaxed);
                injected.store(true, std::memory_order_relaxed);
                throw std::bad_alloc{};
            }
        } else if (remaining.compare_exchange_weak(count, count - 1, std::memory_order_relaxed)) {
            return;
        }
    }
}

void Arm(std::size_t budget) {
    failureBytes.store(0U, std::memory_order_relaxed);
    injected.store(false, std::memory_order_relaxed);
    remaining.store(static_cast<std::int64_t>(budget), std::memory_order_relaxed);
}

void Disarm() noexcept { remaining.store(-1, std::memory_order_relaxed); }
} // namespace allocation_fault

void* operator new(std::size_t bytes) {
    allocation_fault::BeforeAllocate(bytes);
    if (void* pointer = std::malloc(bytes != 0U ? bytes : 1U)) return pointer;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void* operator new(std::size_t bytes, std::align_val_t alignment) {
    allocation_fault::BeforeAllocate(bytes);
    if (void* pointer = _aligned_malloc(bytes != 0U ? bytes : 1U, static_cast<std::size_t>(alignment))) return pointer;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t bytes, std::align_val_t alignment) { return ::operator new(bytes, alignment); }
void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept {
    try { return ::operator new(bytes); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept {
    try { return ::operator new[](bytes); } catch (...) { return nullptr; }
}
void* operator new(std::size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try { return ::operator new(bytes, alignment); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try { return ::operator new[](bytes, alignment); } catch (...) { return nullptr; }
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::align_val_t) noexcept { _aligned_free(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { _aligned_free(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { _aligned_free(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { _aligned_free(pointer); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { _aligned_free(pointer); }
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { _aligned_free(pointer); }
#endif

namespace {

using kb::ecs::Entity;
using kb::ecs::GeneratedEntityIndex;
using kb::ecs::World;

struct Value { std::uint32_t token = 0U; };
struct Cold { std::array<std::uint32_t, 128U> cells{}; };

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Entity Full(std::uint32_t index, std::uint32_t generation = 0U) {
    return Entity{ (static_cast<Entity::IdType>(generation) << 32U)
        | (kb::ecs::kGeneratedEntityIndexBase + index) };
}

std::vector<Value> Values(std::size_t count, std::uint32_t first = 100U) {
    std::vector<Value> values(count);
    for (std::size_t index = 0U; index < count; ++index) values[index].token = first + static_cast<std::uint32_t>(index);
    return values;
}

std::vector<Entity> Create(World& world, std::span<const Value> values, bool nativeOnly = false) {
    const std::array views{ World::MakeBulkComponentView<Value>(values) };
    return nativeOnly ? world.CreateEntitiesNativeOnly(values.size(), views)
        : world.CreateEntities(values.size(), views);
}

void CheckRows(const World& world, std::span<const Entity> entities,
    std::span<const Value> values, bool mirrored) {
    Require(entities.size() == values.size(), "Created row count differs from input payload count");
    const auto component = world.Component<Value>();
    for (std::size_t index = 0U; index < entities.size(); ++index) {
        Require(world.IsAlive(entities[index]), "Created native full entity ID is not alive");
        const auto* value = world.TryGet<Value>(entities[index]);
        Require(value != nullptr && value->token == values[index].token, "Created native payload was lost or reordered");
        const auto backendId = ecs_strip_generation(entities[index].Id());
        const auto alive = ecs_get_alive(world.NativeHandle(), backendId);
        if (mirrored) {
            Require(alive == backendId, "Mirrored backend row was not established with its expected ID");
            const auto* backend = static_cast<const Value*>(ecs_get_id(world.NativeHandle(), alive, component));
            Require(backend != nullptr && backend->token == values[index].token, "Mirrored backend payload differs from native input");
        } else {
            Require(alive == 0U, "Native-only creation made an unexpected backend row");
        }
    }
}

ecs_entity_t Occupy(World& world, std::uint32_t index, std::uint32_t generation, std::uint32_t token) {
    const ecs_entity_t entity = Full(index, generation).Id();
    ecs_make_alive(world.NativeHandle(), entity);
    const Value value{ token };
    ecs_set_id(world.NativeHandle(), entity, world.RegisterComponent<Value>(), sizeof(Value), &value);
    return entity;
}

void CheckRaw(const World& world, ecs_entity_t entity, std::uint32_t token) {
    Require(ecs_get_alive(world.NativeHandle(), ecs_strip_generation(entity)) == entity,
        "Native allocation changed raw backend owner liveness or generation");
    const auto* value = static_cast<const Value*>(ecs_get_id(world.NativeHandle(), entity, world.Component<Value>()));
    Require(value != nullptr && value->token == token, "Native allocation overwrote an independently owned backend payload");
}

void CheckIndices(std::span<const Entity> entities, std::span<const std::uint32_t> indices,
    std::uint32_t generation) {
    Require(entities.size() == indices.size(), "Expected index ledger size differs from allocation count");
    for (std::size_t index = 0U; index < entities.size(); ++index) {
        Require(entities[index] == Full(indices[index], generation), "Native slot order or logical generation changed unexpectedly");
    }
}

void RunOccupied() {
    for (const std::uint32_t generation : { 0U, 7U }) {
        World world;
        const auto raw = Occupy(world, 0U, generation, 901U);
        const Entity first = world.CreateEntity();
        Require(first == Full(1U), "Single creation did not skip the occupied first generated slot");
        world.Set(first, Value{ 11U });
        CheckRaw(world, raw, 901U);
        ecs_delete(world.NativeHandle(), raw);
        const Entity released = world.CreateEntity();
        Require(released == Full(0U), "A never-issued blocked placeholder was lost or had its generation incremented");
    }
    World world;
    const std::array raw{
        Occupy(world, 0U, 0U, 910U), Occupy(world, 2U, 5U, 912U), Occupy(world, 5U, 9U, 915U)
    };
    const auto values = Values(7U);
    const auto entities = Create(world, values);
    const std::array<std::uint32_t, 7U> expected{ 1U, 3U, 4U, 6U, 7U, 8U, 9U };
    CheckIndices(entities, expected, 0U);
    CheckRows(world, entities, values, true);
    CheckRaw(world, raw[0], 910U);
    CheckRaw(world, raw[1], 912U);
    CheckRaw(world, raw[2], 915U);
    for (auto entity : raw) ecs_delete(world.NativeHandle(), entity);
    const auto released = Create(world, Values(3U));
    const std::array<std::uint32_t, 3U> releasedIndices{ 0U, 2U, 5U };
    CheckIndices(released, releasedIndices, 0U);
}

void RunBlockedFreeOrder() {
    World world;
    const auto initial = Create(world, Values(6U));
    world.DestroyEntities(initial);
    const std::array raw{
        Occupy(world, 1U, 7U, 921U), Occupy(world, 3U, 7U, 923U), Occupy(world, 4U, 7U, 924U)
    };
    const auto values = Values(2U, 200U);
    const auto selected = Create(world, values);
    const std::array<std::uint32_t, 2U> selectedIndices{ 2U, 5U };
    CheckIndices(selected, selectedIndices, 1U);
    CheckRows(world, selected, values, true);
    CheckRaw(world, raw[0], 921U);
    CheckRaw(world, raw[1], 923U);
    CheckRaw(world, raw[2], 924U);
    ecs_delete(world.NativeHandle(), raw[1]);
    Require(world.CreateEntity() == Full(3U, 1U), "A released interleaved free slot was not recovered in LIFO order");
    ecs_delete(world.NativeHandle(), raw[0]);
    ecs_delete(world.NativeHandle(), raw[2]);
    const auto rest = Create(world, Values(3U, 300U));
    const std::array<std::uint32_t, 3U> restIndices{ 0U, 1U, 4U };
    CheckIndices(rest, restIndices, 1U);
}

void RunConfigurations() {
    for (const bool mirrored : { false, true }) {
        for (const bool valueMirroring : { false, true }) {
            for (const bool nativeOnly : { false, true }) {
                kb::ecs::WorldConfig config;
                config.mirrorEntitiesToBackend = mirrored;
                config.mirrorNativeComponentChangesToBackend = valueMirroring;
                config.trackEntityCatalog = false;
                World world{ config };
                const auto raw = Occupy(world, 0U, 11U, 950U);
                const auto values = Values(5U, 500U);
                const auto entities = Create(world, values, nativeOnly);
                const std::array<std::uint32_t, 5U> expected{ 1U, 2U, 3U, 4U, 5U };
                CheckIndices(entities, expected, 0U);
                CheckRows(world, entities, values, mirrored && !nativeOnly);
                world.DestroyEntities(entities);
                Require(world.NativeStorageStats().liveEntities == 0U, "Configuration bulk destroy left live native rows");
                CheckRaw(world, raw, 950U);
            }
        }
    }
}

struct StreamSource {
    kb::ecs::ChunkedWorldSnapshotHeader header;
    kb::ecs::ChunkedWorldSnapshotChunk chunk;
    bool delivered = false;
};

kb::ecs::ChunkedWorldSnapshotStreamReadResult ReadChunk(kb::ecs::ChunkedWorldSnapshotChunk& output, void* context) {
    auto& source = *static_cast<StreamSource*>(context);
    if (source.delivered) return kb::ecs::ChunkedWorldSnapshotStreamReadResult::End;
    source.delivered = true;
    output = source.chunk;
    return kb::ecs::ChunkedWorldSnapshotStreamReadResult::Chunk;
}

void PrepareStream(StreamSource& source, kb::ecs::ComponentId component,
    std::span<const Entity> entities, std::span<const Value> values) {
    source = {};
    source.header.entityCount = entities.size();
    for (Entity entity : entities) source.chunk.entityIds.push_back(entity.Id());
    kb::ecs::ChunkedComponentSnapshot payload;
    payload.componentId = component;
    payload.componentSize = sizeof(Value);
    payload.data.resize(values.size_bytes());
    std::memcpy(payload.data.data(), values.data(), values.size_bytes());
    source.chunk.components.push_back(std::move(payload));
}

bool Restore(World& world, StreamSource& source) {
    source.delivered = false;
    return world.RestoreChunkedSnapshotStream(source.header, &ReadChunk, &source);
}

void RunAdoptCollision() {
    for (const std::uint32_t logicalGeneration : { 0U, 17U }) {
        StreamSource source;
        World world;
        const Entity sentinel = world.CreateEntity();
        world.Set(sentinel, Value{ 771U });
        std::vector<Entity> entities;
        for (std::uint32_t index = 0U; index < 19U; ++index) entities.push_back(Full(100U + index, logicalGeneration));
        const auto values = Values(entities.size(), 700U);
        PrepareStream(source, world.Component<Value>(), entities, values);
        const auto raw = Occupy(world, 118U, 23U, 999U);
        const auto beforeVersion = world.NativeStorage().StructuralVersion();
        bool rejected = false;
        try { rejected = !Restore(world, source); }
        catch (const std::invalid_argument&) { rejected = true; }
        Require(rejected, "Snapshot adoption accepted a live raw backend index late in its bulk input");
        Require(world.NativeStorageStats().liveEntities == 1U
            && world.NativeStorage().StructuralVersion() == beforeVersion,
            "Rejected bulk adoption changed native storage before prevalidation completed");
        Require(world.IsAlive(sentinel) && world.TryGet<Value>(sentinel)->token == 771U,
            "Rejected bulk adoption changed the unrelated native owner");
        for (Entity entity : entities) Require(!world.IsAlive(entity), "Rejected bulk adoption partially committed a native owner");
        for (std::size_t index = 0U; index + 1U < entities.size(); ++index) {
            Require(ecs_get_alive(world.NativeHandle(), ecs_strip_generation(entities[index].Id())) == 0U,
                "Rejected bulk adoption partially committed a backend row");
        }
        CheckRaw(world, raw, 999U);
        ecs_delete(world.NativeHandle(), raw);
        Require(Restore(world, source), "Snapshot adoption did not recover after the raw owner was released");
        CheckRows(world, entities, values, true);
        Require(world.NativeStorageStats().liveEntities == entities.size() + 1U,
            "Successful adoption has an inconsistent live count");
    }
}

struct EventLedger {
    std::vector<Entity> ids;
    std::vector<std::uint32_t> tokens;
};

void RecordAdopt(Entity entity, kb::ecs::ComponentEventKind, const Value* value, void* context) {
    auto& ledger = *static_cast<EventLedger*>(context);
    ledger.ids.push_back(entity);
    ledger.tokens.push_back(value != nullptr ? value->token : 0U);
}

void RunFarAdopt() {
    EventLedger ledger;
    ledger.ids.reserve(8U);
    ledger.tokens.reserve(8U);
    StreamSource source;
    World world;
    const auto component = world.RegisterComponent<Value>();
    const std::array entities{
        Entity{ (37ULL << 32U) | 3'000'000'000ULL },
        Entity{ (41ULL << 32U) | 4'000'000'000ULL }
    };
    const std::array values{ Value{ 811U }, Value{ 812U } };
    PrepareStream(source, component, entities, values);
    const auto observer = world.ObserveComponent<Value>(kb::ecs::ComponentEventKind::Modified, &RecordAdopt, &ledger);
    Require(observer != 0U && Restore(world, source), "Sparse far full-ID adoption failed");
    CheckRows(world, entities, values, true);
    Require(ledger.ids.size() == entities.size(), "Far adoption emitted a wrong managed event count");
    for (std::size_t index = 0U; index < entities.size(); ++index) {
        const auto found = std::find(ledger.ids.begin(), ledger.ids.end(), entities[index]);
        Require(found != ledger.ids.end(), "Far adoption observer received a truncated or stale logical ID");
        const auto eventIndex = static_cast<std::size_t>(found - ledger.ids.begin());
        Require(ledger.tokens[eventIndex] == values[index].token, "Far adoption observer payload differs from its owner");
    }
    const Entity normal = world.CreateEntity();
    Require(normal != entities[0] && normal != entities[1] && world.IsAlive(normal),
        "Normal generated allocation collided with a far adopted owner");
    world.DestroyObserver(observer);
}

void RunMovedSkippedSlots() {
    kb::ecs::WorldConfig config;
    config.mirrorEntitiesToBackend = false;
    config.mirrorNativeComponentChangesToBackend = false;
    World original{ config };
    const auto raw = Occupy(original, 0U, 29U, 971U);
    Require(original.CreateEntity() == Full(1U), "Move fixture did not retain its first blocked placeholder");
    World moved{ std::move(original) };
    World assigned;
    assigned = std::move(moved);
    Require(original.NativeHandle() == nullptr && moved.NativeHandle() == nullptr,
        "Moved-from World retained a backend pointer");
    Require(assigned.CreateEntity() == Full(2U), "Availability policy context did not follow the moved heap world");
    CheckRaw(assigned, raw, 971U);
    ecs_delete(assigned.NativeHandle(), raw);
    Require(assigned.CreateEntity() == Full(0U), "Moved world lost its skipped placeholder after backend release");
}

void RunNamedNoMirror() {
    kb::ecs::WorldConfig config;
    config.mirrorEntitiesToBackend = false;
    config.mirrorNativeComponentChangesToBackend = false;
    World world{ config };
    const Entity entity = world.CreateEntity("named-native-only");
    Require(world.IsAlive(entity), "Named native-only creation did not establish a native owner");
    Require(ecs_get_alive(world.NativeHandle(), ecs_strip_generation(entity.Id())) == 0U,
        "Named creation bypassed mirrorEntitiesToBackend=false");
}

#if defined(KB_NATIVE_POLICY_FAULT_INJECTION)
void RunFaultRecovery(std::size_t sweepCount) {
    std::size_t allocationFailures = 0U;
    std::size_t appendFailures = 0U;
    std::size_t successes = 0U;
    for (std::size_t budget = 0U; budget < sweepCount; ++budget) {
        kb::ecs::WorldConfig config;
        config.mirrorEntitiesToBackend = false;
        config.mirrorNativeComponentChangesToBackend = false;
        config.trackEntityCatalog = false;
        config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk4KB;
        World world{ config };
        const auto warmValues = Values(7U);
        std::vector<Cold> warmCold(warmValues.size());
        for (std::size_t index = 0U; index < warmCold.size(); ++index) warmCold[index].cells.fill(static_cast<std::uint32_t>(index + 1000U));
        const kb::ecs::ComponentRegistrationOptions coldOptions{ .storageClass = kb::ecs::ComponentStorageClass::ColdTable };
        const std::array warmViews{
            World::MakeBulkComponentView<Value>(warmValues),
            World::MakeBulkComponentView<Cold>(warmCold, coldOptions)
        };
        const auto initial = world.CreateEntities(7U, warmViews);
        const Entity sentinel = initial.back();
        world.DestroyEntities(std::span<const Entity>{ initial.data(), 6U });
        const std::array raw{
            Occupy(world, 1U, 7U, 991U), Occupy(world, 3U, 7U, 993U),
            Occupy(world, 4U, 7U, 994U), Occupy(world, 7U, 7U, 997U)
        };
        const auto beforeVersion = world.NativeStorage().StructuralVersion();
        const std::size_t count = world.NativeStorageStats().capacity + 17U;
        const auto values = Values(count, 2000U);
        std::vector<Cold> cold(count);
        for (std::size_t index = 0U; index < cold.size(); ++index) cold[index].cells.fill(static_cast<std::uint32_t>(index + 3000U));
        const std::array views{
            World::MakeBulkComponentView<Value>(values),
            World::MakeBulkComponentView<Cold>(cold, coldOptions)
        };
        std::vector<Entity> output;
        bool failed = false;
        allocation_fault::Arm(budget);
        try { world.CreateEntitiesInto(output, count, views); }
        catch (const std::bad_alloc&) { failed = true; }
        catch (...) { allocation_fault::Disarm(); throw; }
        allocation_fault::Disarm();
        CheckRaw(world, raw[0], 991U);
        CheckRaw(world, raw[1], 993U);
        CheckRaw(world, raw[2], 994U);
        CheckRaw(world, raw[3], 997U);
        Require(world.IsAlive(sentinel) && world.TryGet<Value>(sentinel)->token == warmValues.back().token,
            "Allocation failure changed the surviving native value");
        const auto* survivorCold = world.TryGet<Cold>(sentinel);
        Require(survivorCold != nullptr && survivorCold->cells == warmCold.back().cells,
            "Allocation failure changed the surviving cold payload");
        if (failed) {
            Require(allocation_fault::injected.load(std::memory_order_relaxed), "Uncontrolled bad_alloc occurred during fault recovery");
            const bool duringAppend = world.NativeStorage().StructuralVersion() != beforeVersion;
            duringAppend ? ++appendFailures : ++allocationFailures;
            Require(output.empty() && world.NativeStorageStats().liveEntities == 1U,
                "Failed bulk allocation/append left committed output or orphaned native rows");
            const auto snapshot = world.CaptureChunkedSnapshot();
            Require(snapshot.entityCount == 1U, "Failed append left unreachable rows in the native query snapshot");
            for (auto entity : raw) ecs_delete(world.NativeHandle(), entity);
            const auto retryValues = Values(6U, 4000U);
            const auto retry = Create(world, retryValues);
            const std::array<std::uint32_t, 6U> expected{ 0U, 1U, 2U, 3U, 4U, 5U };
            CheckIndices(retry, expected, 1U);
            CheckRows(world, retry, retryValues, false);
            Require(world.CreateEntity() == Full(7U), "Failed allocation retained a dangling new-placeholder free index");
            std::printf("FAULT budget=%zu bytes=%zu phase=%s\n", budget,
                allocation_fault::failureBytes.load(std::memory_order_relaxed), duringAppend ? "append" : "allocation");
        } else {
            ++successes;
            CheckRows(world, output, values, false);
            Require(world.NativeStorageStats().liveEntities == count + 1U, "Successful fault-sweep batch has an inconsistent live count");
        }
    }
    std::printf("FAULT_COVERAGE allocation=%zu append=%zu successful=%zu\n", allocationFailures, appendFailures, successes);
    Require(allocationFailures != 0U && appendFailures != 0U && successes != 0U,
        "Fault sweep did not cover allocation failure, append failure and successful recovery");
}
#endif

struct Test { const char* name; void (*run)(); };

} // namespace

int main(int argc, char** argv) {
    const std::array tests{
        Test{ "occupied", &RunOccupied }, Test{ "blocked-free-order", &RunBlockedFreeOrder },
        Test{ "configurations", &RunConfigurations }, Test{ "adopt-collision", &RunAdoptCollision },
        Test{ "far-adopt", &RunFarAdopt }, Test{ "moved-skipped", &RunMovedSkippedSlots },
        Test{ "named-no-mirror", &RunNamedNoMirror }
    };
    try {
#if defined(KB_NATIVE_POLICY_FAULT_INJECTION)
        if (argc > 1 && std::string_view{ argv[1] } == "fault-recovery") {
            const std::size_t count = argc > 2 ? static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10)) : 128U;
            std::printf("START fault-recovery\n");
            std::fflush(stdout);
            RunFaultRecovery(count);
            std::printf("PASS fault-recovery\n");
            return 0;
        }
#endif
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
        Require(selected, "Unknown allocation-policy selector, or fault-recovery requires KB_NATIVE_POLICY_FAULT_INJECTION");
        return 0;
    } catch (const std::exception& error) {
#if defined(KB_NATIVE_POLICY_FAULT_INJECTION)
        allocation_fault::Disarm();
#endif
        std::fprintf(stderr, "FAIL native allocation policy: %s\n", error.what());
        return 1;
    }
}
