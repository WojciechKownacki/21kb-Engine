#pragma once

#include "TestSupport.hpp"
#include "../src/private/ecs/QueryStateFactory.hpp"
#include "engine/ecs/NativeArchetypeStorage.hpp"
#include "engine/ecs/QueryExecutionScratch.hpp"
#include "engine/ecs/WorkerPool.hpp"
#include "engine/ecs/World.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {

struct ChangedQueryPosition {
    std::uint32_t row = 0U;
    std::uint32_t value = 0U;
};

struct ChangedQueryVelocity {
    std::uint32_t value = 0U;
};

struct ChangedQueryMarker {
    std::uint32_t value = 0U;
};

struct ChangedQueryFixture {
    kb::ecs::WorldConfig config{
        .chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk4KB,
        .mirrorEntitiesToBackend = false,
        .mirrorNativeComponentChangesToBackend = false,
        .trackEntityCatalog = false,
    };
    kb::ecs::World world{ config };
    kb::ecs::ComponentId positionId = world.RegisterComponent<ChangedQueryPosition>();
    kb::ecs::ComponentId velocityId = world.RegisterComponent<ChangedQueryVelocity>();
    std::size_t capacity = 0U;
    std::vector<kb::ecs::Entity> entities;

    ChangedQueryFixture() {
        const std::array types{
            kb::ecs::NativeComponentType{ .id = positionId, .size = sizeof(ChangedQueryPosition), .alignment = alignof(ChangedQueryPosition) },
            kb::ecs::NativeComponentType{ .id = velocityId, .size = sizeof(ChangedQueryVelocity), .alignment = alignof(ChangedQueryVelocity) },
        };
        capacity = kb::ecs::EstimateNativeArchetypeCapacity(types, config.chunkSizeProfile).entitiesPerChunk;
        std::vector<ChangedQueryPosition> positions(capacity * 3U + 11U);
        for (std::size_t row = 0U; row < positions.size(); ++row) {
            positions[row].row = static_cast<std::uint32_t>(row);
        }
        const ChangedQueryVelocity velocity{};
        const std::array views{
            kb::ecs::World::MakeBulkComponentView<ChangedQueryPosition>(positions),
            kb::ecs::World::MakeBulkComponentBroadcastView(velocity),
        };
        entities = world.CreateEntitiesNativeOnly(positions.size(), views);
        auto query = world.CreateQuery<ChangedQueryPosition, ChangedQueryVelocity>();
        kb::ecs::QueryBatchExecutionScratch scratch;
        query.PrepareBatchExecution({}, scratch);
        kb::tests::Require(scratch.records_.size() == 4U, "Changed query fixture requires three full chunks and a tail");
        for (std::size_t index = 0U; index < 3U; ++index) {
            kb::tests::Require(scratch.records_[index].entityCount == capacity, "Changed query fixture has an incomplete full chunk");
        }
        kb::tests::Require(scratch.records_.back().entityCount == 11U, "Changed query fixture tail has incorrect size");
    }

    auto Query() {
        kb::ecs::QueryFilter filter;
        filter.Changed(positionId);
        return world.CreateQuery<ChangedQueryPosition, ChangedQueryVelocity>(filter);
    }

    void ChangeTail() {
        world.Set(entities.back(), ChangedQueryPosition{
            .row = static_cast<std::uint32_t>(entities.size() - 1U),
            .value = 19U,
        });
    }
};

struct ChangedQueryVisits {
    std::size_t size = 0U;
    std::unique_ptr<std::atomic_uint[]> rows;
    std::atomic_size_t total{ 0U };

    explicit ChangedQueryVisits(std::size_t count)
        : size(count), rows(std::make_unique<std::atomic_uint[]>(count)) {
        Reset();
    }

    void Reset() {
        total.store(0U, std::memory_order_relaxed);
        for (std::size_t row = 0U; row < size; ++row) {
            rows[row].store(0U, std::memory_order_relaxed);
        }
    }

    void Visit(const ChangedQueryPosition& position) {
        kb::tests::Require(position.row < size, "Changed query returned a corrupt row identity");
        rows[position.row].fetch_add(1U, std::memory_order_relaxed);
        total.fetch_add(1U, std::memory_order_relaxed);
    }

    void RequireVisits(unsigned expected, const char* message) const {
        const std::size_t actual = total.load(std::memory_order_relaxed);
        if (actual != size * expected) {
            std::cerr << "Changed query rows: expected=" << size * expected << " actual=" << actual << '\n';
        }
        kb::tests::Require(actual == size * expected, message);
        for (std::size_t row = 0U; row < size; ++row) {
            kb::tests::Require(rows[row].load(std::memory_order_relaxed) == expected, "Changed query skipped or duplicated a row identity");
        }
    }

    void RequireSubset(std::size_t begin, std::size_t count, const char* message) const {
        kb::tests::Require(total.load(std::memory_order_relaxed) == count, message);
        for (std::size_t row = 0U; row < size; ++row) {
            const unsigned expected = row >= begin && row - begin < count ? 1U : 0U;
            kb::tests::Require(rows[row].load(std::memory_order_relaxed) == expected, message);
        }
    }
};

void RunChangedQueryBatchSnapshotTest(kb::ecs::QueryExecutionPolicy policy, bool mutableFirst) {
    ChangedQueryFixture fixture;
    auto query = fixture.Query();
    kb::ecs::WorkerPool pool(kb::ecs::WorkerPoolConfig{ .workerCount = 3U });
    const kb::ecs::QueryExecutionSettings settings{
        .maxBatchSize = 31U,
        .policy = policy,
        .workerPool = &pool,
    };
    ChangedQueryVisits visits(fixture.entities.size());
    auto read = [&visits](const auto& batch) {
        const auto* positions = batch.template Components<0>();
        for (std::size_t row = 0U; row < batch.Count(); ++row) {
            visits.Visit(positions[row]);
        }
    };
    if (mutableFirst) {
        query.ForEachMutableBatchKernel(settings, [&visits](auto& batch) {
            auto* positions = batch.template Components<0>();
            for (std::size_t row = 0U; row < batch.Count(); ++row) {
                ++positions[row].value;
                visits.Visit(positions[row]);
            }
        });
        visits.RequireVisits(1U, "Changed mutable query skipped chunks during its initial execution");
        visits.Reset();
        query.ForEachBatchKernel(settings, read);
        visits.RequireVisits(1U, "Changed query consumed writes produced after its execution snapshot");
    } else {
        query.ForEachBatchKernel(settings, read);
        visits.RequireVisits(1U, "Changed read query skipped chunks during its initial execution");
    }
    visits.Reset();
    query.ForEachBatchKernel(settings, read);
    visits.RequireVisits(0U, "Changed query revisited an unchanged archetype");
    fixture.world.Set(fixture.entities.back(), ChangedQueryVelocity{ .value = 7U });
    query.ForEachBatchKernel(settings, read);
    visits.RequireVisits(0U, "Changed query selected a write to an unrelated component");
    fixture.ChangeTail();
    query.ForEachBatchKernel(settings, read);
    visits.RequireVisits(1U, "Changed query failed to revisit all chunks after a tail write");
    visits.Reset();
    query.ForEachBatchKernel(settings, read);
    visits.RequireVisits(0U, "Changed query did not commit its successful read snapshot");
}

void RunChangedQueryRowSnapshotTest(bool raw) {
    ChangedQueryFixture fixture;
    auto query = fixture.Query();
    ChangedQueryVisits visits(fixture.entities.size());
    using RawState = std::unique_ptr<kb::ecs::QueryState, void (*)(kb::ecs::QueryState*) noexcept>;
    const std::array ids{ fixture.positionId, fixture.velocityId };
    const std::array sizes{ sizeof(ChangedQueryPosition), sizeof(ChangedQueryVelocity) };
    const std::array changed{ fixture.positionId };
    RawState state(kb::ecs::QueryStateFactory::Create(
        &const_cast<kb::ecs::NativeArchetypeStorage&>(fixture.world.NativeStorage()),
        ids, sizes, {}, {}, {}, changed, fixture.config, nullptr, nullptr), &kb::ecs::DestroyQueryState);
    const auto run = [&] {
        if (raw) {
            kb::ecs::ForEachQueryState(state.get(), [](kb::ecs::Entity, const void* const* components, void* context) {
                static_cast<ChangedQueryVisits*>(context)->Visit(*static_cast<const ChangedQueryPosition*>(components[0]));
            }, &visits);
        } else {
            query.ForEach([](kb::ecs::Entity, const ChangedQueryPosition& position, const ChangedQueryVelocity&, void* context) {
                static_cast<ChangedQueryVisits*>(context)->Visit(position);
            }, &visits);
        }
    };
    run();
    visits.RequireVisits(1U, "Changed row query skipped chunks during its initial execution");
    visits.Reset();
    run();
    visits.RequireVisits(0U, "Changed row query revisited unchanged chunks");
    fixture.ChangeTail();
    run();
    visits.RequireVisits(1U, "Changed row query skipped chunks after a tail write");
}

void RunChangedQueryExceptionSnapshotTest(kb::ecs::QueryExecutionPolicy policy, bool mutableWrite) {
    ChangedQueryFixture fixture;
    auto query = fixture.Query();
    kb::ecs::WorkerPool pool(kb::ecs::WorkerPoolConfig{ .workerCount = 3U });
    const kb::ecs::QueryExecutionSettings settings{ .maxBatchSize = fixture.capacity, .policy = policy, .workerPool = &pool };
    kb::ecs::QueryBatchExecutionScratch metadata;
    query.PrepareBatchExecution({}, metadata);
    auto& storage = const_cast<kb::ecs::NativeArchetypeStorage&>(fixture.world.NativeStorage());
    const auto archetype = metadata.records_.front().nativeArchetypeIndex;
    const auto beforeVersion = storage.ArchetypeComponentVersion(archetype, fixture.positionId);
    for (const auto& record : metadata.records_) {
        storage.ClearComponentDirtyRows(archetype, record.nativeChunkIndex, fixture.positionId);
    }
    std::atomic_bool threw{ false };
    auto throwing = [&](auto& batch) {
        auto* positions = batch.template Components<0>();
        if constexpr (!std::is_const_v<std::remove_pointer_t<decltype(positions)>>) {
            ++positions[0].value;
        }
        if (positions[0].row >= fixture.capacity * 2U && !threw.exchange(true, std::memory_order_relaxed)) {
            throw std::runtime_error("Changed query callback failure");
        }
    };
    bool caught = false;
    try {
        if (mutableWrite) {
            query.ForEachMutableBatchKernel(settings, throwing);
        } else {
            query.ForEachBatchKernel(settings, throwing);
        }
    } catch (const std::runtime_error&) {
        caught = true;
    }
    kb::tests::Require(caught && threw.load(std::memory_order_relaxed), "Changed query failed to propagate callback exception");
    if (mutableWrite) {
        kb::tests::Require(storage.ArchetypeComponentVersion(archetype, fixture.positionId) > beforeVersion,
            "Mutable query exception left partial writes without a component version");
        kb::tests::Require(storage.ArchetypeComponentDirtyCount(archetype, fixture.positionId) > 0U,
            "Mutable query exception left partial writes without dirty rows");
        const auto* value = static_cast<const ChangedQueryPosition*>(storage.ComponentData(fixture.entities[fixture.capacity * 2U], fixture.positionId));
        kb::tests::Require(value->value == 1U, "Mutable query exception failed to retain its partial write");
        kb::tests::Require(storage.ComponentDirtyCount(archetype, metadata.records_[2].nativeChunkIndex, fixture.positionId) > 0U,
            "Mutable query exception failed to publish the throwing range");
    }
    ChangedQueryVisits visits(fixture.entities.size());
    query.ForEachBatchKernel(settings, [&visits](const auto& batch) {
        const auto* positions = batch.template Components<0>();
        for (std::size_t row = 0U; row < batch.Count(); ++row) {
            visits.Visit(positions[row]);
        }
    });
    visits.RequireVisits(1U, "Changed query consumed a failed execution before a complete retry");
    visits.Reset();
    query.ForEachBatchKernel(settings, [&visits](const auto& batch) {
        const auto* positions = batch.template Components<0>();
        for (std::size_t row = 0U; row < batch.Count(); ++row) {
            visits.Visit(positions[row]);
        }
    });
    visits.RequireVisits(0U, "Changed query retained a completed retry");
}

void RunChangedQueryCallbackMutationTest(kb::ecs::QueryExecutionPolicy policy) {
    ChangedQueryFixture fixture;
    auto query = fixture.Query();
    kb::ecs::WorkerPool pool(kb::ecs::WorkerPoolConfig{ .workerCount = 3U });
    const kb::ecs::QueryExecutionSettings settings{ .maxBatchSize = fixture.capacity, .policy = policy, .workerPool = &pool };
    ChangedQueryVisits visits(fixture.entities.size());
    std::atomic_bool mutated{ false };
    query.ForEachBatchKernel(settings, [&](const auto& batch) {
        const auto* positions = batch.template Components<0>();
        if (!mutated.exchange(true, std::memory_order_relaxed)) {
            ChangedQueryPosition value = positions[0];
            value.value = 29U;
            fixture.world.Set(batch.EntityAt(0U), value);
        }
        for (std::size_t row = 0U; row < batch.Count(); ++row) {
            visits.Visit(positions[row]);
        }
    });
    visits.RequireVisits(1U, "Changed query changed chunk selection during callback mutation");
    visits.Reset();
    query.ForEachBatchKernel(settings, [&visits](const auto& batch) {
        const auto* positions = batch.template Components<0>();
        for (std::size_t row = 0U; row < batch.Count(); ++row) {
            visits.Visit(positions[row]);
        }
    });
    visits.RequireVisits(1U, "Changed query consumed a callback mutation newer than its snapshot");
}

void RunChangedQueryRequiredComponentFilterTest() {
    ChangedQueryFixture fixture;
    kb::ecs::QueryFilter filter;
    filter.Require(fixture.velocityId).Changed(fixture.velocityId).Changed(fixture.positionId);
    auto query = fixture.world.CreateQuery<ChangedQueryPosition>(filter);
    ChangedQueryVisits visits(fixture.entities.size());
    auto read = [&visits](const auto& batch) {
        const auto* positions = batch.template Components<0>();
        for (std::size_t row = 0U; row < batch.Count(); ++row) visits.Visit(positions[row]);
    };
    query.ForEachBatchKernel(read);
    visits.RequireVisits(1U, "Changed query skipped chunks with multiple filtered components");
    visits.Reset();
    query.ForEachBatchKernel(read);
    visits.RequireVisits(0U, "Changed query did not drain multiple filter observations");
    fixture.world.Set(fixture.entities.back(), ChangedQueryVelocity{ .value = 3U });
    query.ForEachBatchKernel(read);
    visits.RequireVisits(1U, "Changed query ignored a filtered required component outside its selected columns");
    visits.Reset();
    query.ForEachBatchKernel(read);
    visits.RequireVisits(0U, "Changed query failed to consume a required component snapshot");
    fixture.ChangeTail();
    query.ForEachBatchKernel(read);
    visits.RequireVisits(1U, "Changed query required every filtered component to change instead of any component");
}

void RunChangedQueryUnselectedArchetypeMutationTest() {
    ChangedQueryFixture fixture;
    const std::size_t originalRows = fixture.entities.size();
    std::vector<ChangedQueryPosition> positions(5U);
    for (std::size_t row = 0U; row < positions.size(); ++row) positions[row].row = static_cast<std::uint32_t>(originalRows + row);
    const ChangedQueryVelocity velocity{};
    const ChangedQueryMarker marker{};
    const std::array views{
        kb::ecs::World::MakeBulkComponentView<ChangedQueryPosition>(positions),
        kb::ecs::World::MakeBulkComponentBroadcastView(velocity),
        kb::ecs::World::MakeBulkComponentBroadcastView(marker),
    };
    const auto marked = fixture.world.CreateEntitiesNativeOnly(positions.size(), views);
    kb::ecs::QueryFilter filter;
    filter.Require(fixture.velocityId).Changed(fixture.velocityId);
    auto query = fixture.world.CreateQuery<ChangedQueryPosition>(filter);
    ChangedQueryVisits visits(originalRows + marked.size());
    auto read = [&visits](const auto& batch) {
        const auto* values = batch.template Components<0>();
        for (std::size_t row = 0U; row < batch.Count(); ++row) visits.Visit(values[row]);
    };
    query.ForEachBatchKernel(read);
    visits.RequireVisits(1U, "Changed query failed to observe both initial archetypes");
    visits.Reset();
    fixture.world.Set(fixture.entities.front(), ChangedQueryVelocity{ .value = 4U });
    bool mutated = false;
    query.ForEachBatchKernel([&](const auto& batch) {
        if (!mutated) {
            mutated = true;
            fixture.world.Set(marked.front(), ChangedQueryVelocity{ .value = 5U });
        }
        read(batch);
    });
    kb::tests::Require(mutated, "Changed query failed to execute the changed archetype");
    visits.RequireSubset(0U, originalRows, "Changed query selected an initially unchanged archetype after callback mutation");
    visits.Reset();
    query.ForEachBatchKernel(read);
    visits.RequireSubset(originalRows, marked.size(), "Changed query consumed a skipped archetype's newer callback mutation");
}

void RunChangedQueryReentrantSnapshotTest() {
    ChangedQueryFixture fixture;
    auto query = fixture.Query();
    ChangedQueryVisits outer(fixture.entities.size());
    ChangedQueryVisits inner(fixture.entities.size());
    bool nested = false;
    auto readInner = [&inner](const auto& batch) {
        const auto* positions = batch.template Components<0>();
        for (std::size_t row = 0U; row < batch.Count(); ++row) inner.Visit(positions[row]);
    };
    query.ForEachBatchKernel([&](const auto& batch) {
        if (!nested) {
            nested = true;
            fixture.ChangeTail();
            query.ForEachBatchKernel(readInner);
        }
        const auto* positions = batch.template Components<0>();
        for (std::size_t row = 0U; row < batch.Count(); ++row) outer.Visit(positions[row]);
    });
    outer.RequireVisits(1U, "A reentrant Changed query altered the outer chunk selection");
    inner.RequireVisits(1U, "A reentrant Changed query skipped chunks of its newer snapshot");
    inner.Reset();
    query.ForEachBatchKernel(readInner);
    inner.RequireVisits(0U, "An outer Changed commit rolled back a newer reentrant observation");
}

void RunChangedQueryStructuralMutationGuardTest() {
    ChangedQueryFixture fixture;
    auto query = fixture.Query();
    struct Context {
        kb::ecs::World* world;
        kb::ecs::Entity entity;
    } context{ &fixture.world, fixture.entities.back() };
    bool blocked = false;
    try {
        // Typed rows use QueryState::ForEach, the same raw record executor.
        query.ForEach([](kb::ecs::Entity, const ChangedQueryPosition&, const ChangedQueryVelocity&, void* raw) {
            auto& state = *static_cast<Context*>(raw);
            state.world->DestroyEntity(state.entity);
        }, &context);
    } catch (const std::logic_error&) {
        blocked = true;
    }
    kb::tests::Require(blocked && fixture.world.IsAlive(context.entity),
        "Changed raw record execution allowed structural pointer invalidation");
    ChangedQueryVisits visits(fixture.entities.size());
    auto read = [&visits](const auto& batch) {
        const auto* positions = batch.template Components<0>();
        for (std::size_t row = 0U; row < batch.Count(); ++row) visits.Visit(positions[row]);
    };
    query.ForEachBatchKernel(read);
    visits.RequireVisits(1U, "A blocked structural mutation consumed the Changed snapshot");
    fixture.world.DestroyEntity(context.entity);
    kb::tests::Require(!fixture.world.IsAlive(context.entity),
        "Changed raw record execution retained its structural guard after failure");
}

void RunEcsChangedQueryTests() {
    constexpr std::array policies{
        kb::ecs::QueryExecutionPolicy::SingleThread,
        kb::ecs::QueryExecutionPolicy::SingleThreadSIMD,
        kb::ecs::QueryExecutionPolicy::ParallelChunks,
        kb::ecs::QueryExecutionPolicy::ParallelRanges,
        kb::ecs::QueryExecutionPolicy::SIMDPreferred,
        kb::ecs::QueryExecutionPolicy::ParallelSIMD,
        kb::ecs::QueryExecutionPolicy::Deterministic,
        kb::ecs::QueryExecutionPolicy::StreamingLargeWorld,
    };
    for (auto policy : policies) {
        RunChangedQueryBatchSnapshotTest(policy, false);
        RunChangedQueryBatchSnapshotTest(policy, true);
    }
    RunChangedQueryRowSnapshotTest(false);
    RunChangedQueryRowSnapshotTest(true);
    for (auto policy : { kb::ecs::QueryExecutionPolicy::SingleThread, kb::ecs::QueryExecutionPolicy::ParallelRanges }) {
        RunChangedQueryExceptionSnapshotTest(policy, false);
        RunChangedQueryExceptionSnapshotTest(policy, true);
        RunChangedQueryCallbackMutationTest(policy);
    }
    RunChangedQueryRequiredComponentFilterTest();
    RunChangedQueryUnselectedArchetypeMutationTest();
    RunChangedQueryReentrantSnapshotTest();
    RunChangedQueryStructuralMutationGuardTest();
}

} // namespace

