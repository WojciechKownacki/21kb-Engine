#include "EcsTestTypes.hpp"
#include "EcsTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/WorkerPool.hpp"
#include "engine/ecs/World.hpp"

#include <algorithm>
#include <atomic>
#include <initializer_list>
#include <stdexcept>
#include <vector>

namespace {

void CountMovingBatches(const kb::ecs::QueryBatch<EcsPosition, EcsVelocity>& batch, void* context) {
    auto* counters = static_cast<EcsBatchCounters*>(context);
    ++counters->batches;
    counters->maxBatch = std::max(counters->maxBatch, batch.Count());

    const EcsPosition* positions = batch.Components<0>();
    const EcsVelocity* velocities = batch.Components<1>();
    for (std::size_t index = 0; index < batch.Count(); ++index) {
        kb::tests::Require(batch.EntityAt(index).IsValid(), "Typed ECS batch query returned invalid entity");
        ++counters->visited;
        counters->sumX += positions[index].x + velocities[index].x;
    }
}

void CountMovingRows(kb::ecs::Entity entity, const EcsPosition& position, const EcsVelocity& velocity, void* context) {
    kb::tests::Require(entity.IsValid(), "Typed ECS row query returned invalid entity");
    auto* counters = static_cast<EcsIterationCounters*>(context);
    ++counters->visited;
    counters->sumX += position.x + velocity.x;
}

void CountPositions(kb::ecs::Entity entity, const EcsPosition& position, void* context) {
    kb::tests::Require(entity.IsValid(), "Filtered ECS query returned invalid entity");
    auto* counters = static_cast<EcsIterationCounters*>(context);
    ++counters->visited;
    counters->sumX += position.x;
}

void IntegrateMovingBatches(kb::ecs::MutableQueryBatch<EcsPosition, EcsVelocity>& batch, void* context) {
    auto* counters = static_cast<EcsBatchCounters*>(context);
    ++counters->batches;
    counters->maxBatch = std::max(counters->maxBatch, batch.Count());

    EcsPosition* positions = batch.Components<0>();
    EcsVelocity* velocities = batch.Components<1>();
    for (std::size_t index = 0; index < batch.Count(); ++index) {
        kb::tests::Require(batch.EntityAt(index).IsValid(), "Mutable ECS batch query returned invalid entity");
        positions[index].x += velocities[index].x;
        ++counters->visited;
        counters->sumX += positions[index].x;
    }
}

std::vector<float> CollectPositionOrder(kb::ecs::Query<EcsPosition>& query, kb::ecs::QueryExecutionSettings settings) {
    std::vector<float> values;
    query.ForEachBatchKernel(settings, [&values](const kb::ecs::QueryBatch<EcsPosition>& batch) {
        const EcsPosition* positions = batch.Components<0>();
        for (std::size_t index = 0; index < batch.Count(); ++index) {
            kb::tests::Require(batch.EntityAt(index).IsValid(), "Ordered ECS query returned invalid entity");
            values.push_back(positions[index].x);
        }
    });
    return values;
}

struct MovingQuerySnapshot {
    std::vector<kb::ecs::Entity::IdType> entityIds;
    float sumX = 0.0F;
};

struct ParallelQueryCounters {
    std::atomic<std::size_t> visited = 0;
    std::atomic<std::size_t> batches = 0;
    std::atomic<std::size_t> maxBatch = 0;
    std::atomic<int> sumX = 0;
};

void TrackMaxBatch(std::atomic<std::size_t>& target, std::size_t value) noexcept {
    std::size_t current = target.load(std::memory_order_acquire);
    while (current < value && !target.compare_exchange_weak(current, value, std::memory_order_acq_rel, std::memory_order_acquire)) {
    }
}

void CountMovingBatchesParallel(const kb::ecs::QueryBatch<EcsPosition, EcsVelocity>& batch, void* context) {
    auto* counters = static_cast<ParallelQueryCounters*>(context);
    counters->batches.fetch_add(1U, std::memory_order_acq_rel);
    TrackMaxBatch(counters->maxBatch, batch.Count());

    const EcsPosition* positions = batch.Components<0>();
    const EcsVelocity* velocities = batch.Components<1>();
    int sum = 0;
    for (std::size_t index = 0; index < batch.Count(); ++index) {
        kb::tests::Require(batch.EntityAt(index).IsValid(), "Parallel ECS batch query returned invalid entity");
        sum += static_cast<int>(positions[index].x + velocities[index].x);
    }

    counters->visited.fetch_add(batch.Count(), std::memory_order_acq_rel);
    counters->sumX.fetch_add(sum, std::memory_order_acq_rel);
}

MovingQuerySnapshot CollectMovingSnapshot(kb::ecs::Query<EcsPosition, EcsVelocity>& query) {
    MovingQuerySnapshot snapshot;
    query.ForEachBatchKernel(kb::ecs::QueryExecutionSettings{
                                 .maxBatchSize = 2,
                                 .iterationOrder = kb::ecs::QueryIterationOrder::Deterministic,
                             },
                             [&snapshot](const kb::ecs::QueryBatch<EcsPosition, EcsVelocity>& batch) {
                                 const EcsPosition* positions = batch.Components<0>();
                                 const EcsVelocity* velocities = batch.Components<1>();
                                 for (std::size_t index = 0; index < batch.Count(); ++index) {
                                     const kb::ecs::Entity entity = batch.EntityAt(index);
                                     kb::tests::Require(entity.IsValid(), "Structural ECS query returned an invalid entity");
                                     snapshot.entityIds.push_back(entity.Id());
                                     snapshot.sumX += positions[index].x + velocities[index].x;
                                 }
                             });
    std::sort(snapshot.entityIds.begin(), snapshot.entityIds.end());
    return snapshot;
}

void RequireMovingSnapshot(
    const MovingQuerySnapshot& snapshot,
    std::initializer_list<kb::ecs::Entity> expectedEntities,
    float expectedSum,
    const char* message) {
    std::vector<kb::ecs::Entity::IdType> expectedIds;
    expectedIds.reserve(expectedEntities.size());
    for (kb::ecs::Entity entity : expectedEntities) {
        expectedIds.push_back(entity.Id());
    }
    std::sort(expectedIds.begin(), expectedIds.end());

    kb::tests::Require(snapshot.entityIds == expectedIds, message);
    kb::tests::Require(kb::tests::NearlyEqual(snapshot.sumX, expectedSum), "Structural ECS query read stale component values");
}

void RunTypedEcsQueryRowBatchAdapterTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 64,
    });
    for (int index = 0; index < 20; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 0.0F });
        world.Set(entity, EcsVelocity{ .x = static_cast<float>(index * 2), .y = 0.0F });
    }

    kb::ecs::Query<EcsPosition, EcsVelocity> query = world.CreateQuery<EcsPosition, EcsVelocity>();
    EcsIterationCounters counters;
    query.ForEach(&CountMovingRows, &counters);
    kb::tests::Require(counters.visited == 20, "Typed ECS row query did not visit all entities");
    kb::tests::Require(kb::tests::NearlyEqual(counters.sumX, 570.0F), "Typed ECS row query did not advance component pointers inside batches");
}

void RunTypedEcsQueryBatchTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 128,
    });
    for (int index = 0; index < 600; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = 1.0F, .y = 0.0F });
        world.Set(entity, EcsVelocity{ .x = 2.0F, .y = 0.0F });
    }

    kb::ecs::Query<EcsPosition, EcsVelocity> query = world.CreateQuery<EcsPosition, EcsVelocity>();
    EcsBatchCounters counters;
    query.ForEachBatch(&CountMovingBatches, &counters);
    kb::tests::Require(counters.visited == 600, "Typed ECS batch query did not visit all entities");
    kb::tests::Require(counters.batches >= 5, "Typed ECS batch query did not split work into execution grains");
    kb::tests::Require(counters.maxBatch <= 128, "Typed ECS batch query exceeded configured execution grain size");
    kb::tests::Require(kb::tests::NearlyEqual(counters.sumX, 1800.0F), "Typed ECS batch query saw invalid component data");
}

void RunTypedEcsQueryBatchWorkStealingTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 32,
    });
    for (int index = 0; index < 512; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = 1.0F, .y = 0.0F });
        world.Set(entity, EcsVelocity{ .x = 2.0F, .y = 0.0F });
    }

    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 2 } };
    kb::ecs::Query<EcsPosition, EcsVelocity> query = world.CreateQuery<EcsPosition, EcsVelocity>();

    ParallelQueryCounters counters;
    query.ForEachBatch(
        kb::ecs::QueryExecutionSettings{
            .maxBatchSize = 32,
            .iterationOrder = kb::ecs::QueryIterationOrder::ChunkOrder,
            .workerPool = &pool,
        },
        &CountMovingBatchesParallel,
        &counters);

    kb::tests::Require(counters.visited.load(std::memory_order_acquire) == 512U, "Parallel ECS batch query did not visit all entities");
    kb::tests::Require(counters.batches.load(std::memory_order_acquire) >= 16U, "Parallel ECS batch query did not split work into batches");
    kb::tests::Require(counters.maxBatch.load(std::memory_order_acquire) <= 32U, "Parallel ECS batch query exceeded configured batch size");
    kb::tests::Require(counters.sumX.load(std::memory_order_acquire) == 1536, "Parallel ECS batch query read invalid component data");
}

void RunTypedEcsMutableQueryBatchTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 4,
    });
    const kb::ecs::ComponentId positionId = world.RegisterComponent<EcsPosition>();

    for (int index = 0; index < 10; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 0.0F });
        world.Set(entity, EcsVelocity{ .x = 2.0F, .y = 0.0F });
    }

    kb::ecs::QueryFilter changedFilter;
    changedFilter.Changed(positionId);
    kb::ecs::Query<EcsPosition> changedQuery = world.CreateQuery<EcsPosition>(changedFilter);

    EcsIterationCounters initialChanged;
    changedQuery.ForEach(&CountPositions, &initialChanged);
    kb::tests::Require(initialChanged.visited == 10, "Changed ECS query did not visit the initial mutable batch table");

    EcsIterationCounters unchanged;
    changedQuery.ForEach(&CountPositions, &unchanged);
    kb::tests::Require(unchanged.visited == 0, "Changed ECS query was not cleared before mutable batch write");

    kb::ecs::Query<EcsPosition, EcsVelocity> movingQuery = world.CreateQuery<EcsPosition, EcsVelocity>();
    EcsBatchCounters mutableCounters;
    movingQuery.ForEachMutableBatch(kb::ecs::QueryExecutionSettings{ .maxBatchSize = 3 }, &IntegrateMovingBatches, &mutableCounters);
    kb::tests::Require(mutableCounters.visited == 10, "Mutable ECS batch query did not visit all entities");
    kb::tests::Require(mutableCounters.batches >= 4, "Mutable ECS batch query did not split work into configured batches");
    kb::tests::Require(mutableCounters.maxBatch <= 3, "Mutable ECS batch query exceeded configured max batch size");
    kb::tests::Require(kb::tests::NearlyEqual(mutableCounters.sumX, 65.0F), "Mutable ECS batch query did not write expected component values");

    movingQuery.ForEachMutableBatchKernel([](kb::ecs::MutableQueryBatch<EcsPosition, EcsVelocity>& batch) {
        EcsPosition* positions = batch.Components<0>();
        const EcsVelocity* velocities = batch.Components<1>();
        for (std::size_t index = 0; index < batch.Count(); ++index) {
            positions[index].x += velocities[index].x;
        }
    });

    EcsIterationCounters changedAfterMutable;
    changedQuery.ForEach(&CountPositions, &changedAfterMutable);
    kb::tests::Require(changedAfterMutable.visited == 10, "Mutable ECS batch query did not mark written components as changed");
    kb::tests::Require(kb::tests::NearlyEqual(changedAfterMutable.sumX, 85.0F), "Mutable ECS batch query did not persist component writes");
}

void RunTypedEcsQueryBatchKernelTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 96,
    });
    for (int index = 0; index < 300; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 0.0F });
        world.Set(entity, EcsVelocity{ .x = 1.0F, .y = 0.0F });
    }

    kb::ecs::Query<EcsPosition, EcsVelocity> query = world.CreateQuery<EcsPosition, EcsVelocity>();
    EcsBatchCounters counters;
    query.ForEachBatchKernel([&counters](const kb::ecs::QueryBatch<EcsPosition, EcsVelocity>& batch) {
        ++counters.batches;
        counters.maxBatch = std::max(counters.maxBatch, batch.Count());

        const EcsPosition* positions = batch.Components<0>();
        const EcsVelocity* velocities = batch.Components<1>();
        for (std::size_t index = 0; index < batch.Count(); ++index) {
            kb::tests::Require(batch.EntityAt(index).IsValid(), "Typed ECS batch kernel returned invalid entity");
            ++counters.visited;
            counters.sumX += positions[index].x + velocities[index].x;
        }
    });

    kb::tests::Require(counters.visited == 300, "Typed ECS batch kernel did not visit all entities");
    kb::tests::Require(counters.batches >= 4, "Typed ECS batch kernel did not split work into execution grains");
    kb::tests::Require(counters.maxBatch <= 96, "Typed ECS batch kernel exceeded configured execution grain size");
    kb::tests::Require(kb::tests::NearlyEqual(counters.sumX, 45150.0F), "Typed ECS batch kernel saw invalid component data");
}

void RunTypedEcsQueryPrefetchHintTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 16,
    });
    for (int index = 0; index < 64; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = 1.0F, .y = 0.0F });
        world.Set(entity, EcsVelocity{ .x = 2.0F, .y = 0.0F });
    }

    kb::ecs::Query<EcsPosition, EcsVelocity> query = world.CreateQuery<EcsPosition, EcsVelocity>();
    EcsBatchCounters readCounters;
    query.ForEachBatchKernel(kb::ecs::QueryExecutionSettings{
                                 .maxBatchSize = 16,
                                 .iterationOrder = kb::ecs::QueryIterationOrder::StorageOrder,
                                 .prefetchDistance = 8,
                             },
                             [&readCounters](const kb::ecs::QueryBatch<EcsPosition, EcsVelocity>& batch) {
                                 ++readCounters.batches;
                                 const EcsPosition* positions = batch.Components<0>();
                                 const EcsVelocity* velocities = batch.Components<1>();
                                 for (std::size_t index = 0; index < batch.Count(); ++index) {
                                     batch.Prefetch(index + 4);
                                     ++readCounters.visited;
                                     readCounters.sumX += positions[index].x + velocities[index].x;
                                 }
                             });

    kb::tests::Require(readCounters.visited == 64, "Prefetched ECS batch query did not visit all entities");
    kb::tests::Require(readCounters.batches == 4, "Prefetched ECS batch query did not use the configured batch size");
    kb::tests::Require(kb::tests::NearlyEqual(readCounters.sumX, 192.0F), "Prefetched ECS batch query read invalid component data");

    EcsBatchCounters mutableCounters;
    query.ForEachMutableBatchKernel(kb::ecs::QueryExecutionSettings{
                                        .maxBatchSize = 16,
                                        .iterationOrder = kb::ecs::QueryIterationOrder::StorageOrder,
                                        .prefetchDistance = 8,
                                    },
                                    [&mutableCounters](kb::ecs::MutableQueryBatch<EcsPosition, EcsVelocity>& batch) {
                                        EcsPosition* positions = batch.Components<0>();
                                        const EcsVelocity* velocities = batch.Components<1>();
                                        for (std::size_t index = 0; index < batch.Count(); ++index) {
                                            batch.Prefetch(index + 4);
                                            positions[index].x += velocities[index].x;
                                            ++mutableCounters.visited;
                                            mutableCounters.sumX += positions[index].x;
                                        }
                                    });

    kb::tests::Require(mutableCounters.visited == 64, "Prefetched mutable ECS batch query did not visit all entities");
    kb::tests::Require(kb::tests::NearlyEqual(mutableCounters.sumX, 192.0F), "Prefetched mutable ECS batch query did not persist writes");

    kb::ecs::Query<EcsPosition> positionQuery = world.CreateQuery<EcsPosition>();
    EcsIterationCounters singleComponentCounters;
    positionQuery.ForEachBatchKernel(kb::ecs::QueryExecutionSettings{
                                         .maxBatchSize = 7,
                                         .iterationOrder = kb::ecs::QueryIterationOrder::StorageOrder,
                                         .prefetchDistance = 3,
                                     },
                                     [&singleComponentCounters](const kb::ecs::QueryBatch<EcsPosition>& batch) {
                                         const EcsPosition* positions = batch.Components<0>();
                                         for (std::size_t index = 0; index < batch.Count(); ++index) {
                                             batch.Prefetch(index + 2);
                                             ++singleComponentCounters.visited;
                                             singleComponentCounters.sumX += positions[index].x;
                                         }
                                     });

    kb::tests::Require(singleComponentCounters.visited == 64, "Single-component ECS query fast path did not visit all entities");
    kb::tests::Require(kb::tests::NearlyEqual(singleComponentCounters.sumX, 192.0F), "Single-component ECS query fast path read invalid component data");
}

void RunTypedEcsQueryIterationOrderPolicyTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 2,
    });

    const kb::ecs::Entity movingA = world.CreateEntity();
    world.Set(movingA, EcsPosition{ .x = 20.0F, .y = 0.0F });
    world.Set(movingA, EcsVelocity{ .x = 1.0F, .y = 0.0F });

    const kb::ecs::Entity plainA = world.CreateEntity();
    world.Set(plainA, EcsPosition{ .x = 10.0F, .y = 0.0F });

    const kb::ecs::Entity movingB = world.CreateEntity();
    world.Set(movingB, EcsPosition{ .x = 21.0F, .y = 0.0F });
    world.Set(movingB, EcsVelocity{ .x = 1.0F, .y = 0.0F });

    const kb::ecs::Entity plainB = world.CreateEntity();
    world.Set(plainB, EcsPosition{ .x = 11.0F, .y = 0.0F });

    kb::ecs::Query<EcsPosition> query = world.CreateQuery<EcsPosition>();

    const std::vector<float> deterministic = CollectPositionOrder(query, kb::ecs::QueryExecutionSettings{
                                                                             .maxBatchSize = 8,
                                                                             .iterationOrder = kb::ecs::QueryIterationOrder::Deterministic,
                                                                         });
    const std::vector<float> expected{ 10.0F, 11.0F, 20.0F, 21.0F };
    kb::tests::Require(deterministic == expected, "Deterministic ECS query iteration did not order archetypes by stable type signature");

    const std::vector<float> storage = CollectPositionOrder(query, kb::ecs::QueryExecutionSettings{
                                                                       .maxBatchSize = 2,
                                                                       .iterationOrder = kb::ecs::QueryIterationOrder::StorageOrder,
                                                                   });
    kb::tests::Require(storage.size() == expected.size(), "Storage-order ECS query policy did not visit all entities");

    const std::vector<float> chunk = CollectPositionOrder(query, kb::ecs::QueryExecutionSettings{
                                                                     .maxBatchSize = 2,
                                                                     .iterationOrder = kb::ecs::QueryIterationOrder::ChunkOrder,
                                                                 });
    kb::tests::Require(chunk.size() == expected.size(), "Chunk-order ECS query policy did not visit all entities");
}

void RunTypedEcsQueryPlanCacheReuseTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 32,
    });

    kb::ecs::Query<EcsPosition, EcsVelocity> firstQuery = world.CreateQuery<EcsPosition, EcsVelocity>();
    kb::ecs::Query<EcsPosition, EcsVelocity> secondQuery = world.CreateQuery<EcsPosition, EcsVelocity>();

    for (int index = 0; index < 75; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 0.0F });
        world.Set(entity, EcsVelocity{ .x = 3.0F, .y = 0.0F });
    }

    EcsBatchCounters firstCounters;
    firstQuery.ForEachBatch(&CountMovingBatches, &firstCounters);

    EcsBatchCounters secondCounters;
    secondQuery.ForEachBatch(&CountMovingBatches, &secondCounters);

    kb::tests::Require(firstCounters.visited == 75, "Cached ECS query plan did not visit all entities through first query");
    kb::tests::Require(secondCounters.visited == 75, "Cached ECS query plan did not visit all entities through second query");
    kb::tests::Require(kb::tests::NearlyEqual(firstCounters.sumX, secondCounters.sumX), "Cached ECS query plan produced inconsistent repeated query results");
    kb::tests::Require(kb::tests::NearlyEqual(firstCounters.sumX, 3000.0F), "Cached ECS query plan saw invalid component data after structural changes");
}

void RunTypedEcsQueryStructuralChangeConsistencyTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 2,
    });

    const kb::ecs::Entity stable = world.CreateEntity();
    world.Set(stable, EcsPosition{ .x = 1.0F, .y = 0.0F });
    world.Set(stable, EcsVelocity{ .x = 10.0F, .y = 0.0F });

    const kb::ecs::Entity removedVelocity = world.CreateEntity();
    world.Set(removedVelocity, EcsPosition{ .x = 2.0F, .y = 0.0F });
    world.Set(removedVelocity, EcsVelocity{ .x = 10.0F, .y = 0.0F });

    const kb::ecs::Entity destroyed = world.CreateEntity();
    world.Set(destroyed, EcsPosition{ .x = 4.0F, .y = 0.0F });
    world.Set(destroyed, EcsVelocity{ .x = 10.0F, .y = 0.0F });

    const kb::ecs::Entity gainedVelocity = world.CreateEntity();
    world.Set(gainedVelocity, EcsPosition{ .x = 8.0F, .y = 0.0F });

    kb::ecs::Query<EcsPosition, EcsVelocity> query = world.CreateQuery<EcsPosition, EcsVelocity>();
    RequireMovingSnapshot(
        CollectMovingSnapshot(query),
        { stable, removedVelocity, destroyed },
        37.0F,
        "Structural ECS query initial set did not match pre-migration archetypes");

    world.Remove<EcsVelocity>(removedVelocity);
    world.DestroyEntity(destroyed);
    world.Set(gainedVelocity, EcsVelocity{ .x = 10.0F, .y = 0.0F });

    const kb::ecs::Entity createdAfterQuery = world.CreateEntity();
    world.Set(createdAfterQuery, EcsPosition{ .x = 16.0F, .y = 0.0F });
    world.Set(createdAfterQuery, EcsVelocity{ .x = 10.0F, .y = 0.0F });

    RequireMovingSnapshot(
        CollectMovingSnapshot(query),
        { stable, gainedVelocity, createdAfterQuery },
        55.0F,
        "Existing ECS query did not stay consistent after same-frame structural changes");

    kb::ecs::Query<EcsPosition, EcsVelocity> freshQuery = world.CreateQuery<EcsPosition, EcsVelocity>();
    RequireMovingSnapshot(
        CollectMovingSnapshot(freshQuery),
        { stable, gainedVelocity, createdAfterQuery },
        55.0F,
        "Fresh ECS query did not match existing query after structural changes invalidated cached plans");
}

void RunTypedEcsQueryComponentFilterTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 16,
    });

    const kb::ecs::ComponentId velocityId = world.RegisterComponent<EcsVelocity>();
    const kb::ecs::ComponentId markerId = world.RegisterComponent<EcsQueryMarker>();
    const kb::ecs::ComponentId disabledId = world.RegisterComponent<EcsDisabled>();

    const kb::ecs::Entity moving = world.CreateEntity();
    world.Set(moving, EcsPosition{ .x = 1.0F, .y = 0.0F });
    world.Set(moving, EcsVelocity{ .x = 1.0F, .y = 0.0F });

    const kb::ecs::Entity still = world.CreateEntity();
    world.Set(still, EcsPosition{ .x = 2.0F, .y = 0.0F });

    const kb::ecs::Entity disabled = world.CreateEntity();
    world.Set(disabled, EcsPosition{ .x = 4.0F, .y = 0.0F });
    world.Set(disabled, EcsVelocity{ .x = 1.0F, .y = 0.0F });
    world.Set(disabled, EcsDisabled{ .value = 1 });

    const kb::ecs::Entity marked = world.CreateEntity();
    world.Set(marked, EcsPosition{ .x = 8.0F, .y = 0.0F });
    world.Set(marked, EcsVelocity{ .x = 1.0F, .y = 0.0F });
    world.Set(marked, EcsQueryMarker{ .value = 1 });

    kb::ecs::QueryFilter filter;
    filter.Require(velocityId).Optional(markerId).Exclude(disabledId);
    kb::ecs::Query<EcsPosition> query = world.CreateQuery<EcsPosition>(filter);

    EcsIterationCounters counters;
    query.ForEach(&CountPositions, &counters);

    kb::tests::Require(counters.visited == 2, "Filtered ECS query did not apply required/optional/excluded component terms");
    kb::tests::Require(kb::tests::NearlyEqual(counters.sumX, 9.0F), "Filtered ECS query returned an unexpected entity set");
}

void RunTypedEcsQueryChangeFilterTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 16,
    });

    const kb::ecs::ComponentId positionId = world.RegisterComponent<EcsPosition>();

    kb::ecs::Entity edited;
    for (int index = 0; index < 4; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index + 1), .y = 0.0F });
        world.Set(entity, EcsVelocity{ .x = 10.0F, .y = 0.0F });
        if (index == 2) {
            edited = entity;
        }
    }
    kb::tests::Require(edited.IsValid(), "Changed ECS query test did not create the edited entity");

    kb::ecs::QueryFilter filter;
    filter.Changed(positionId);
    kb::ecs::Query<EcsPosition, EcsVelocity> query = world.CreateQuery<EcsPosition, EcsVelocity>(filter);

    EcsBatchCounters first;
    query.ForEachBatch(&CountMovingBatches, &first);
    kb::tests::Require(first.visited == 4, "Changed ECS query did not visit initial matching table");
    kb::tests::Require(kb::tests::NearlyEqual(first.sumX, 50.0F), "Changed ECS query did not read selected component data");

    EcsBatchCounters unchanged;
    query.ForEachBatch(&CountMovingBatches, &unchanged);
    kb::tests::Require(unchanged.visited == 0, "Changed ECS query revisited an unchanged table");

    world.Set(edited, EcsVelocity{ .x = 25.0F, .y = 0.0F });
    EcsBatchCounters velocityOnly;
    query.ForEachBatch(&CountMovingBatches, &velocityOnly);
    kb::tests::Require(velocityOnly.visited == 0, "Changed ECS query reacted to a non-filtered component write");

    world.Set(edited, EcsPosition{ .x = 20.0F, .y = 0.0F });
    EcsBatchCounters positionChanged;
    query.ForEachBatch(&CountMovingBatches, &positionChanged);
    kb::tests::Require(positionChanged.visited == 4, "Changed ECS query did not revisit a table after a filtered component write");
    kb::tests::Require(kb::tests::NearlyEqual(positionChanged.sumX, 82.0F), "Changed ECS query returned stale component data after a filtered write");
}

void RunTypedEcsQueryFilterValidationTest() {
    kb::ecs::World world;
    const kb::ecs::ComponentId positionId = world.RegisterComponent<EcsPosition>();
    const kb::ecs::ComponentId velocityId = world.RegisterComponent<EcsVelocity>();

    bool invalidIdRejected = false;
    try {
        kb::ecs::QueryFilter filter;
        filter.Require(0);
    } catch (const std::invalid_argument&) {
        invalidIdRejected = true;
    }
    kb::tests::Require(invalidIdRejected, "ECS query filter accepted an invalid component id");

    bool conflictRejected = false;
    try {
        kb::ecs::QueryFilter filter;
        filter.Require(velocityId).Exclude(velocityId);
    } catch (const std::invalid_argument&) {
        conflictRejected = true;
    }
    kb::tests::Require(conflictRejected, "ECS query filter accepted conflicting component terms");

    bool requireOptionalRejected = false;
    try {
        kb::ecs::QueryFilter filter;
        filter.Optional(velocityId).Require(velocityId);
    } catch (const std::invalid_argument&) {
        requireOptionalRejected = true;
    }
    kb::tests::Require(requireOptionalRejected, "ECS query filter allowed a required component to remain optional");

    bool optionalSelectedRejected = false;
    try {
        kb::ecs::QueryFilter filter;
        filter.Optional(positionId);
        [[maybe_unused]] kb::ecs::Query<EcsPosition> query = world.CreateQuery<EcsPosition>(filter);
    } catch (const std::invalid_argument&) {
        optionalSelectedRejected = true;
    }
    kb::tests::Require(optionalSelectedRejected, "ECS query filter allowed an optional selected query component");

    bool optionalChangedRejected = false;
    try {
        kb::ecs::QueryFilter filter;
        filter.Optional(velocityId).Changed(velocityId);
    } catch (const std::invalid_argument&) {
        optionalChangedRejected = true;
    }
    kb::tests::Require(optionalChangedRejected, "ECS query filter allowed an optional change-filtered component");
}

} // namespace

namespace kb::tests {

void RunEcsQueryTests() {
    RunTypedEcsQueryRowBatchAdapterTest();
    RunTypedEcsQueryBatchTest();
    RunTypedEcsQueryBatchWorkStealingTest();
    RunTypedEcsMutableQueryBatchTest();
    RunTypedEcsQueryBatchKernelTest();
    RunTypedEcsQueryPrefetchHintTest();
    RunTypedEcsQueryIterationOrderPolicyTest();
    RunTypedEcsQueryPlanCacheReuseTest();
    RunTypedEcsQueryStructuralChangeConsistencyTest();
    RunTypedEcsQueryComponentFilterTest();
    RunTypedEcsQueryChangeFilterTest();
    RunTypedEcsQueryFilterValidationTest();
}

} // namespace kb::tests
