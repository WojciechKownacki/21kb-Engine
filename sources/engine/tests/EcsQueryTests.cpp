#include "EcsTestTypes.hpp"
#include "EcsTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/QueryExecutionScratch.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"
#include "engine/ecs/WorkerPool.hpp"
#include "engine/ecs/World.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

struct alignas(32) EcsAlignedQueryPayload {
    std::array<float, 8U> values{};
};

[[nodiscard]] bool IsAlignedAddress(const void* pointer, std::size_t alignment) noexcept {
    return pointer != nullptr && alignment != 0U && (reinterpret_cast<std::uintptr_t>(pointer) % alignment) == 0U;
}

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
    std::atomic<std::uint32_t> workerMask = 0;
};

struct ReductionQueryCounters {
    std::size_t visited = 0;
    std::size_t batches = 0;
    std::size_t maxBatch = 0;
    bool workerContextActive = false;
};

struct QueryReductionScratchSlot {
    std::size_t visited = 0;
    std::size_t batches = 0;
    std::size_t maxBatch = 0;
    int sumX = 0;
    std::uint32_t workerMask = 0;
};

struct QueryReductionScratchContext {
    kb::ecs::QueryReductionScratch<QueryReductionScratchSlot>* scratch = nullptr;
};

void TrackMaxBatch(std::atomic<std::size_t>& target, std::size_t value) noexcept {
    std::size_t current = target.load(std::memory_order_acquire);
    while (current < value && !target.compare_exchange_weak(current, value, std::memory_order_acq_rel, std::memory_order_acquire)) {
    }
}

void CountMovingBatchesParallel(const kb::ecs::QueryBatch<EcsPosition, EcsVelocity>& batch, void* context) {
    auto* counters = static_cast<ParallelQueryCounters*>(context);
    const kb::ecs::QueryWorkerContext workerContext = kb::ecs::CurrentQueryWorkerContext();
    kb::tests::Require(workerContext.active, "Parallel ECS batch query did not expose worker context");
    kb::tests::Require(workerContext.workerIndex < workerContext.workerCount, "Parallel ECS batch query exposed an invalid worker index");
    if (workerContext.workerIndex < 32U) {
        counters->workerMask.fetch_or(1U << workerContext.workerIndex, std::memory_order_acq_rel);
    }
    counters->batches.fetch_add(1U, std::memory_order_acq_rel);
    TrackMaxBatch(counters->maxBatch, batch.Count());
    std::this_thread::sleep_for(std::chrono::microseconds{ 50 });

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

void CountMovingBatchesParallelFast(const kb::ecs::QueryBatch<EcsPosition, EcsVelocity>& batch, void* context) {
    auto* counters = static_cast<ParallelQueryCounters*>(context);
    const kb::ecs::QueryWorkerContext workerContext = kb::ecs::CurrentQueryWorkerContext();
    kb::tests::Require(workerContext.active, "Fast parallel ECS batch query did not expose worker context");
    kb::tests::Require(workerContext.workerIndex < workerContext.workerCount, "Fast parallel ECS batch query exposed an invalid worker index");
    if (workerContext.workerIndex < 32U) {
        counters->workerMask.fetch_or(1U << workerContext.workerIndex, std::memory_order_acq_rel);
    }
    counters->batches.fetch_add(1U, std::memory_order_acq_rel);
    TrackMaxBatch(counters->maxBatch, batch.Count());

    const EcsPosition* positions = batch.Components<0>();
    const EcsVelocity* velocities = batch.Components<1>();
    int sum = 0;
    for (std::size_t index = 0; index < batch.Count(); ++index) {
        kb::tests::Require(batch.EntityAt(index).IsValid(), "Fast parallel ECS batch query returned invalid entity");
        sum += static_cast<int>(positions[index].x + velocities[index].x);
    }

    counters->visited.fetch_add(batch.Count(), std::memory_order_acq_rel);
    counters->sumX.fetch_add(sum, std::memory_order_acq_rel);
}

void CountMovingBatchesReductionMode(const kb::ecs::QueryBatch<EcsPosition, EcsVelocity>& batch, void* context) {
    auto* counters = static_cast<ReductionQueryCounters*>(context);
    const kb::ecs::QueryWorkerContext workerContext = kb::ecs::CurrentQueryWorkerContext();
    counters->workerContextActive = counters->workerContextActive || workerContext.active;
    ++counters->batches;
    counters->maxBatch = std::max(counters->maxBatch, batch.Count());

    const EcsPosition* positions = batch.Components<0>();
    const EcsVelocity* velocities = batch.Components<1>();
    for (std::size_t index = 0; index < batch.Count(); ++index) {
        kb::tests::Require(batch.EntityAt(index).IsValid(), "Reduction-mode ECS batch query returned invalid entity");
        kb::tests::Require(positions[index].x + velocities[index].x == 3.0F, "Reduction-mode ECS batch query returned invalid component data");
        ++counters->visited;
    }
}

void CountMovingBatchesReductionScratch(const kb::ecs::QueryBatch<EcsPosition, EcsVelocity>& batch, void* context) {
    auto* reductionContext = static_cast<QueryReductionScratchContext*>(context);
    kb::tests::Require(reductionContext != nullptr && reductionContext->scratch != nullptr, "ECS query reduction scratch context is missing");

    const kb::ecs::QueryWorkerContext workerContext = kb::ecs::CurrentQueryWorkerContext();
    kb::tests::Require(workerContext.active, "Per-worker ECS query reduction did not expose worker context");
    kb::tests::Require(workerContext.workerIndex < reductionContext->scratch->SlotCount(), "Per-worker ECS query reduction selected an invalid slot");

    QueryReductionScratchSlot& slot = reductionContext->scratch->SlotForCurrentWorker();
    if (workerContext.workerIndex < 32U) {
        slot.workerMask |= 1U << workerContext.workerIndex;
    }
    ++slot.batches;
    slot.maxBatch = std::max(slot.maxBatch, batch.Count());
    std::this_thread::sleep_for(std::chrono::microseconds{ 50 });

    const EcsPosition* positions = batch.Components<0>();
    const EcsVelocity* velocities = batch.Components<1>();
    for (std::size_t index = 0; index < batch.Count(); ++index) {
        kb::tests::Require(batch.EntityAt(index).IsValid(), "Per-worker ECS query reduction returned invalid entity");
        slot.sumX += static_cast<int>(positions[index].x + velocities[index].x);
    }
    slot.visited += batch.Count();
}

void IntegrateMovingBatchesParallel(kb::ecs::MutableQueryBatch<EcsPosition, EcsVelocity>& batch, void* context) {
    auto* counters = static_cast<ParallelQueryCounters*>(context);
    const kb::ecs::QueryWorkerContext workerContext = kb::ecs::CurrentQueryWorkerContext();
    kb::tests::Require(workerContext.active, "Parallel mutable ECS batch query did not expose worker context");
    kb::tests::Require(workerContext.workerIndex < workerContext.workerCount, "Parallel mutable ECS batch query exposed an invalid worker index");
    if (workerContext.workerIndex < 32U) {
        counters->workerMask.fetch_or(1U << workerContext.workerIndex, std::memory_order_acq_rel);
    }
    counters->batches.fetch_add(1U, std::memory_order_acq_rel);
    TrackMaxBatch(counters->maxBatch, batch.Count());
    std::this_thread::sleep_for(std::chrono::microseconds{ 50 });

    EcsPosition* positions = batch.Components<0>();
    const EcsVelocity* velocities = batch.Components<1>();
    int sum = 0;
    for (std::size_t index = 0; index < batch.Count(); ++index) {
        kb::tests::Require(batch.EntityAt(index).IsValid(), "Parallel mutable ECS batch query returned invalid entity");
        positions[index].x += velocities[index].x;
        sum += static_cast<int>(positions[index].x);
    }

    counters->visited.fetch_add(batch.Count(), std::memory_order_acq_rel);
    counters->sumX.fetch_add(sum, std::memory_order_acq_rel);
}

[[nodiscard]] std::size_t CountSetBits(std::uint32_t value) noexcept {
    std::size_t count = 0;
    while (value != 0U) {
        value &= value - 1U;
        ++count;
    }
    return count;
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

MovingQuerySnapshot CollectFilteredMovingSnapshot(kb::ecs::Query<EcsPosition, EcsVelocity>& query) {
    MovingQuerySnapshot snapshot;
    query.ForEachMutableBatchKernel(kb::ecs::QueryExecutionSettings{
                                        .maxBatchSize = 7,
                                        .iterationOrder = kb::ecs::QueryIterationOrder::Deterministic,
                                    },
                                    [&snapshot](kb::ecs::MutableQueryBatch<EcsPosition, EcsVelocity>& batch) {
                                        EcsPosition* positions = batch.Components<0>();
                                        const EcsVelocity* velocities = batch.Components<1>();
                                        for (std::size_t index = 0; index < batch.Count(); ++index) {
                                            const kb::ecs::Entity entity = batch.EntityAt(index);
                                            kb::tests::Require(entity.IsValid(), "Filtered mutable ECS query returned an invalid entity");
                                            positions[index].x += 1.0F;
                                            snapshot.entityIds.push_back(entity.Id());
                                            snapshot.sumX += positions[index].x + velocities[index].x;
                                        }
                                    });
    std::sort(snapshot.entityIds.begin(), snapshot.entityIds.end());
    return snapshot;
}

MovingQuerySnapshot BuildFilteredMovingReference(
    kb::ecs::World& world,
    std::span<const kb::ecs::Entity> entities,
    std::uint32_t round) {
    MovingQuerySnapshot snapshot;
    for (kb::ecs::Entity entity : entities) {
        if (!world.IsAlive(entity) || !world.Has<EcsPosition>(entity) || !world.Has<EcsVelocity>(entity) || !world.Has<EcsQueryMarker>(entity)
            || world.Has<EcsDisabled>(entity)) {
            continue;
        }

        EcsPosition* position = world.TryGetMutable<EcsPosition>(entity);
        const EcsVelocity* velocity = world.TryGet<EcsVelocity>(entity);
        kb::tests::Require(position != nullptr && velocity != nullptr, "Filtered ECS reference found an incomplete matching entity");
        position->x += 1.0F;
        snapshot.entityIds.push_back(entity.Id());
        snapshot.sumX += position->x + velocity->x;
    }

    if ((round % 5U) == 0U) {
        std::reverse(snapshot.entityIds.begin(), snapshot.entityIds.end());
    }
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

void RunTypedEcsQueryBatchPointerContractTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 11,
    });

    constexpr int kPlainEntities = 48;
    constexpr int kMarkedEntities = 25;
    for (int index = 0; index < kPlainEntities + kMarkedEntities; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = static_cast<float>(index + 100) });
        world.Set(entity, EcsVelocity{ .x = static_cast<float>(index * 2), .y = static_cast<float>(index + 200) });
        if (index >= kPlainEntities) {
            world.Set(entity, EcsQueryMarker{ .value = index });
        }
    }

    kb::ecs::Query<EcsPosition, EcsVelocity> query = world.CreateQuery<EcsPosition, EcsVelocity>();
    std::size_t readBatches = 0;
    std::size_t readRows = 0;
    query.ForEachBatchKernel(
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 11 },
        [&world, &readBatches, &readRows](const kb::ecs::QueryBatch<EcsPosition, EcsVelocity>& batch) {
            ++readBatches;
            const EcsPosition* positions = batch.Components<0>();
            const EcsVelocity* velocities = batch.Components<1>();
            kb::tests::Require(batch.Count() == 0U || (positions != nullptr && velocities != nullptr), "ECS query batch returned null component columns");

            for (std::size_t row = 0; row < batch.Count(); ++row) {
                const kb::ecs::Entity entity = batch.EntityAt(row);
                const EcsPosition* positionRef = world.TryGet<EcsPosition>(entity);
                const EcsVelocity* velocityRef = world.TryGet<EcsVelocity>(entity);
                kb::tests::Require(positionRef == positions + row, "ECS query batch position pointer does not match EntityAt row");
                kb::tests::Require(velocityRef == velocities + row, "ECS query batch velocity pointer does not match EntityAt row");
                kb::tests::Require(kb::tests::NearlyEqual(positions[row].x * 2.0F, velocities[row].x), "ECS query batch component columns lost row alignment");
                ++readRows;
            }
        });

    kb::tests::Require(readRows == static_cast<std::size_t>(kPlainEntities + kMarkedEntities), "ECS query batch pointer contract did not visit every row");
    kb::tests::Require(readBatches >= 7U, "ECS query batch pointer contract did not exercise split batches");

    std::vector<kb::ecs::Entity> mutatedEntities;
    std::vector<float> expectedPositionX;
    std::vector<float> expectedVelocityX;
    query.ForEachMutableBatchKernel(
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 11 },
        [&mutatedEntities, &expectedPositionX, &expectedVelocityX](kb::ecs::MutableQueryBatch<EcsPosition, EcsVelocity>& batch) {
            EcsPosition* positions = batch.Components<0>();
            EcsVelocity* velocities = batch.Components<1>();
            kb::tests::Require(batch.Count() == 0U || (positions != nullptr && velocities != nullptr), "Mutable ECS query batch returned null component columns");

            for (std::size_t row = 0; row < batch.Count(); ++row) {
                positions[row].x += 1000.0F + static_cast<float>(row);
                velocities[row].x = -velocities[row].x;
                mutatedEntities.push_back(batch.EntityAt(row));
                expectedPositionX.push_back(positions[row].x);
                expectedVelocityX.push_back(velocities[row].x);
            }
        });

    kb::tests::Require(mutatedEntities.size() == readRows, "Mutable ECS query batch pointer contract did not visit every row");
    for (std::size_t index = 0; index < mutatedEntities.size(); ++index) {
        const EcsPosition* position = world.TryGet<EcsPosition>(mutatedEntities[index]);
        const EcsVelocity* velocity = world.TryGet<EcsVelocity>(mutatedEntities[index]);
        kb::tests::Require(position != nullptr && velocity != nullptr, "Mutable ECS query batch pointer contract lost a component");
        kb::tests::Require(kb::tests::NearlyEqual(position->x, expectedPositionX[index]), "Mutable ECS query batch write did not persist position data");
        kb::tests::Require(kb::tests::NearlyEqual(velocity->x, expectedVelocityX[index]), "Mutable ECS query batch write did not persist velocity data");
    }
}

void RunTypedEcsQueryBatchAlignmentContractTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 7,
    });

    constexpr int kEntityCount = 65;
    for (int index = 0; index < kEntityCount; ++index) {
        EcsAlignedQueryPayload payload{};
        payload.values[0] = static_cast<float>(index);
        payload.values[7] = static_cast<float>(index + 700);

        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = static_cast<float>(index + 100) });
        world.Set(entity, payload);
    }

    kb::ecs::Query<EcsPosition, EcsAlignedQueryPayload> query = world.CreateQuery<EcsPosition, EcsAlignedQueryPayload>();

    std::size_t readRows = 0;
    query.ForEachBatchKernel(
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 7 },
        [&readRows](const kb::ecs::QueryBatch<EcsPosition, EcsAlignedQueryPayload>& batch) {
            const EcsPosition* positions = batch.Components<0>();
            const EcsAlignedQueryPayload* payloads = batch.Components<1>();
            kb::tests::Require(batch.Count() == 0U || IsAlignedAddress(positions, alignof(EcsPosition)), "ECS query batch returned an unaligned position column");
            kb::tests::Require(batch.Count() == 0U || IsAlignedAddress(payloads, alignof(EcsAlignedQueryPayload)), "ECS query batch returned an unaligned wide component column");

            for (std::size_t row = 0; row < batch.Count(); ++row) {
                kb::tests::Require(IsAlignedAddress(positions + row, alignof(EcsPosition)), "ECS query batch returned an unaligned position row");
                kb::tests::Require(IsAlignedAddress(payloads + row, alignof(EcsAlignedQueryPayload)), "ECS query batch returned an unaligned wide component row");
                kb::tests::Require(kb::tests::NearlyEqual(payloads[row].values[0], positions[row].x), "ECS query batch alignment test lost row pairing");
                ++readRows;
            }
        });
    kb::tests::Require(readRows == static_cast<std::size_t>(kEntityCount), "ECS query batch alignment test did not visit every row");

    std::size_t writtenRows = 0;
    query.ForEachMutableBatchKernel(
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 7 },
        [&writtenRows](kb::ecs::MutableQueryBatch<EcsPosition, EcsAlignedQueryPayload>& batch) {
            EcsPosition* positions = batch.Components<0>();
            EcsAlignedQueryPayload* payloads = batch.Components<1>();
            kb::tests::Require(batch.Count() == 0U || IsAlignedAddress(positions, alignof(EcsPosition)), "Mutable ECS query batch returned an unaligned position column");
            kb::tests::Require(batch.Count() == 0U || IsAlignedAddress(payloads, alignof(EcsAlignedQueryPayload)), "Mutable ECS query batch returned an unaligned wide component column");

            for (std::size_t row = 0; row < batch.Count(); ++row) {
                kb::tests::Require(IsAlignedAddress(positions + row, alignof(EcsPosition)), "Mutable ECS query batch returned an unaligned position row");
                kb::tests::Require(IsAlignedAddress(payloads + row, alignof(EcsAlignedQueryPayload)), "Mutable ECS query batch returned an unaligned wide component row");
                payloads[row].values[1] = positions[row].x + 10.0F;
                ++writtenRows;
            }
        });
    kb::tests::Require(writtenRows == static_cast<std::size_t>(kEntityCount), "Mutable ECS query batch alignment test did not visit every row");

    std::size_t verifiedRows = 0;
    query.ForEachBatchKernel(
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 7 },
        [&verifiedRows](const kb::ecs::QueryBatch<EcsPosition, EcsAlignedQueryPayload>& batch) {
            const EcsPosition* positions = batch.Components<0>();
            const EcsAlignedQueryPayload* payloads = batch.Components<1>();
            for (std::size_t row = 0; row < batch.Count(); ++row) {
                kb::tests::Require(kb::tests::NearlyEqual(payloads[row].values[1], positions[row].x + 10.0F), "Mutable ECS query batch alignment write did not persist");
                ++verifiedRows;
            }
        });
    kb::tests::Require(verifiedRows == static_cast<std::size_t>(kEntityCount), "ECS query batch alignment verification did not visit every row");
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
            .policy = kb::ecs::QueryExecutionPolicy::ParallelRanges,
            .workerPool = &pool,
        },
        &CountMovingBatchesParallel,
        &counters);

    kb::tests::Require(counters.visited.load(std::memory_order_acquire) == 512U, "Parallel ECS batch query did not visit all entities");
    kb::tests::Require(counters.batches.load(std::memory_order_acquire) >= 16U, "Parallel ECS batch query did not split work into batches");
    kb::tests::Require(counters.maxBatch.load(std::memory_order_acquire) <= 32U, "Parallel ECS batch query exceeded configured batch size");
    kb::tests::Require(counters.sumX.load(std::memory_order_acquire) == 1536, "Parallel ECS batch query read invalid component data");
    kb::tests::Require(CountSetBits(counters.workerMask.load(std::memory_order_acquire)) >= 2U, "Parallel ECS batch query did not use multiple workers");
}

void RunTypedEcsMutableQueryBatchWorkStealingTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 64,
    });
    for (int index = 0; index < 4096; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = 1.0F, .y = 0.0F });
        world.Set(entity, EcsVelocity{ .x = 2.0F, .y = 0.0F });
    }

    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 4 } };
    kb::ecs::Query<EcsPosition, EcsVelocity> query = world.CreateQuery<EcsPosition, EcsVelocity>();

    ParallelQueryCounters counters;
    query.ForEachMutableBatch(
        kb::ecs::QueryExecutionSettings{
            .maxBatchSize = 64,
            .iterationOrder = kb::ecs::QueryIterationOrder::ChunkOrder,
            .policy = kb::ecs::QueryExecutionPolicy::ParallelRanges,
            .workerPool = &pool,
        },
        &IntegrateMovingBatchesParallel,
        &counters);

    kb::tests::Require(counters.visited.load(std::memory_order_acquire) == 4096U, "Parallel mutable ECS batch query did not visit all entities");
    kb::tests::Require(counters.batches.load(std::memory_order_acquire) >= 64U, "Parallel mutable ECS batch query did not split work into batches");
    kb::tests::Require(counters.maxBatch.load(std::memory_order_acquire) <= 64U, "Parallel mutable ECS batch query exceeded configured batch size");
    kb::tests::Require(counters.sumX.load(std::memory_order_acquire) == 12288, "Parallel mutable ECS batch query wrote invalid component data");
    kb::tests::Require(CountSetBits(counters.workerMask.load(std::memory_order_acquire)) >= 2U, "Parallel mutable ECS batch query did not use multiple workers");
}

void RunTypedEcsQueryExecutionPolicyBatchShapeTest() {
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

    ParallelQueryCounters chunkCounters;
    query.ForEachBatch(
        kb::ecs::QueryExecutionSettings{
            .maxBatchSize = 32,
            .policy = kb::ecs::QueryExecutionPolicy::ParallelChunks,
            .workerPool = &pool,
        },
        &CountMovingBatchesParallel,
        &chunkCounters);

    ParallelQueryCounters rangeCounters;
    query.ForEachBatch(
        kb::ecs::QueryExecutionSettings{
            .maxBatchSize = 32,
            .policy = kb::ecs::QueryExecutionPolicy::ParallelRanges,
            .workerPool = &pool,
        },
        &CountMovingBatchesParallel,
        &rangeCounters);

    kb::tests::Require(chunkCounters.visited.load(std::memory_order_acquire) == 512U, "Parallel chunk ECS query did not visit all entities");
    kb::tests::Require(rangeCounters.visited.load(std::memory_order_acquire) == 512U, "Parallel range ECS query did not visit all entities");
    kb::tests::Require(chunkCounters.batches.load(std::memory_order_acquire) == 1U, "Parallel chunk ECS query did not keep the storage chunk intact");
    kb::tests::Require(chunkCounters.maxBatch.load(std::memory_order_acquire) == 512U, "Parallel chunk ECS query did not dispatch the full storage chunk");
    kb::tests::Require(rangeCounters.batches.load(std::memory_order_acquire) >= 16U, "Parallel range ECS query did not split work into ranges");
    kb::tests::Require(rangeCounters.maxBatch.load(std::memory_order_acquire) <= 32U, "Parallel range ECS query exceeded configured range size");
}

void RunTypedEcsAdaptiveQueryGrainTest() {
    constexpr int kEntityCount = 20'000;
    kb::ecs::WorldConfig adaptiveConfig;
    adaptiveConfig.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk512KB;
    adaptiveConfig.executionGrainSize = 32;
    adaptiveConfig.adaptiveQueryExecution = true;

    kb::ecs::World adaptiveWorld(adaptiveConfig);
    kb::ecs::WorldConfig fixedConfig = adaptiveConfig;
    fixedConfig.adaptiveQueryExecution = false;
    kb::ecs::World fixedWorld(fixedConfig);

    for (int index = 0; index < kEntityCount; ++index) {
        const kb::ecs::Entity adaptiveEntity = adaptiveWorld.CreateEntity();
        adaptiveWorld.Set(adaptiveEntity, EcsPosition{ .x = 1.0F, .y = 0.0F });
        adaptiveWorld.Set(adaptiveEntity, EcsVelocity{ .x = 2.0F, .y = 0.0F });

        const kb::ecs::Entity fixedEntity = fixedWorld.CreateEntity();
        fixedWorld.Set(fixedEntity, EcsPosition{ .x = 1.0F, .y = 0.0F });
        fixedWorld.Set(fixedEntity, EcsVelocity{ .x = 2.0F, .y = 0.0F });
    }

    kb::ecs::WorkerPool adaptivePool{ kb::ecs::WorkerPoolConfig{ .workerCount = 4 } };
    kb::ecs::WorkerPool fixedPool{ kb::ecs::WorkerPoolConfig{ .workerCount = 4 } };
    kb::ecs::Query<EcsPosition, EcsVelocity> adaptiveQuery = adaptiveWorld.CreateQuery<EcsPosition, EcsVelocity>();
    kb::ecs::Query<EcsPosition, EcsVelocity> fixedQuery = fixedWorld.CreateQuery<EcsPosition, EcsVelocity>();

    ParallelQueryCounters adaptiveCounters;
    kb::ecs::QueryExecutionSettings adaptiveSettings =
        adaptiveWorld.DefaultQueryExecutionSettings(&adaptivePool, kb::ecs::QueryExecutionPolicy::ParallelRanges);
    adaptiveSettings.telemetryEnabled = true;
    adaptiveQuery.ForEachBatch(
        adaptiveSettings,
        &CountMovingBatchesParallelFast,
        &adaptiveCounters);

    ParallelQueryCounters fixedCounters;
    fixedQuery.ForEachBatch(
        fixedWorld.DefaultQueryExecutionSettings(&fixedPool, kb::ecs::QueryExecutionPolicy::ParallelRanges),
        &CountMovingBatchesParallelFast,
        &fixedCounters);

    kb::tests::Require(adaptiveCounters.visited.load(std::memory_order_acquire) == static_cast<std::size_t>(kEntityCount), "Adaptive ECS query grain did not visit all entities");
    kb::tests::Require(fixedCounters.visited.load(std::memory_order_acquire) == static_cast<std::size_t>(kEntityCount), "Fixed ECS query grain did not visit all entities");
    kb::tests::Require(adaptiveCounters.maxBatch.load(std::memory_order_acquire) >= 8192U, "Adaptive ECS query grain did not expand read-only batches");
    kb::tests::Require(fixedCounters.maxBatch.load(std::memory_order_acquire) <= 32U, "Fixed ECS query grain ignored configured batch size");
    kb::tests::Require(
        adaptiveCounters.batches.load(std::memory_order_acquire) * 32U < fixedCounters.batches.load(std::memory_order_acquire),
        "Adaptive ECS query grain did not materially reduce scheduler work items");
    kb::tests::Require(
        adaptiveCounters.sumX.load(std::memory_order_acquire) == fixedCounters.sumX.load(std::memory_order_acquire),
        "Adaptive ECS query grain changed query results");

    const kb::ecs::WorldTelemetrySnapshot adaptiveSnapshot = adaptiveWorld.TelemetrySnapshot();
    kb::tests::Require(adaptiveSnapshot.queryAdaptiveExecutions == 1, "Adaptive ECS query grain did not report an adaptive execution");
    kb::tests::Require(adaptiveSnapshot.queryMaxEffectiveBatchSize >= 8192U, "Adaptive ECS query grain did not report the expanded batch size");
    kb::tests::Require(adaptiveSnapshot.queryParallelRangeExecutions == 1, "Adaptive ECS query grain did not report the effective range policy");
    kb::tests::Require(adaptiveSnapshot.queryAverageEffectiveBatchSize >= 8192.0, "Adaptive ECS query grain did not report average effective batch size");
}

void RunTypedEcsParallelQueryScratchReservationTest() {
    constexpr int kEntityCount = 4096;
    constexpr std::size_t kBatchSize = 64U;
    constexpr std::size_t kExpectedWorkItems = kEntityCount / kBatchSize;

    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk512KB;
    config.executionGrainSize = kBatchSize;
    config.adaptiveQueryExecution = false;
    kb::ecs::World world(config);

    for (int index = 0; index < kEntityCount; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = 1.0F, .y = 0.0F });
        world.Set(entity, EcsVelocity{ .x = 2.0F, .y = 0.0F });
    }

    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 4 } };
    kb::ecs::Query<EcsPosition, EcsVelocity> query = world.CreateQuery<EcsPosition, EcsVelocity>();
    kb::ecs::QueryBatchExecutionScratch readScratch;
    kb::ecs::QueryBatchExecutionScratch scratch;
    const kb::ecs::QueryExecutionSettings settings{
        .maxBatchSize = kBatchSize,
        .policy = kb::ecs::QueryExecutionPolicy::ParallelRanges,
        .workerPool = &pool,
    };

    std::atomic_size_t readVisited{ 0U };
    query.ForEachBatchKernel(
        settings,
        [&readVisited](const kb::ecs::QueryBatch<EcsPosition, EcsVelocity>& batch) {
            readVisited.fetch_add(batch.Count(), std::memory_order_acq_rel);
        },
        readScratch);

    kb::tests::Require(readVisited.load(std::memory_order_acquire) == static_cast<std::size_t>(kEntityCount), "Reserved read-only ECS query scratch did not visit all entities");
    kb::tests::Require(readScratch.workItems_.size() == kExpectedWorkItems, "Reserved read-only ECS query scratch produced an unexpected work-item count");
    kb::tests::Require(readScratch.chunks_.size() == kExpectedWorkItems, "Reserved read-only ECS query scratch produced an unexpected worker chunk count");

    const std::size_t readWorkItemCapacity = readScratch.workItems_.capacity();
    const std::size_t readChunkCapacity = readScratch.chunks_.capacity();
    readVisited.store(0U, std::memory_order_release);
    query.ForEachBatchKernel(
        settings,
        [&readVisited](const kb::ecs::QueryBatch<EcsPosition, EcsVelocity>& batch) {
            readVisited.fetch_add(batch.Count(), std::memory_order_acq_rel);
        },
        readScratch);

    kb::tests::Require(readVisited.load(std::memory_order_acquire) == static_cast<std::size_t>(kEntityCount), "Reused read-only ECS query scratch did not visit all entities");
    kb::tests::Require(readScratch.workItems_.capacity() == readWorkItemCapacity, "Read-only ECS query scratch grew work-item capacity on reuse");
    kb::tests::Require(readScratch.chunks_.capacity() == readChunkCapacity, "Read-only ECS query scratch grew worker chunk capacity on reuse");

    std::atomic_size_t visited{ 0U };
    query.ForEachMutableBatchKernel(
        settings,
        [&visited](kb::ecs::MutableQueryBatch<EcsPosition, EcsVelocity>& batch) {
            visited.fetch_add(batch.Count(), std::memory_order_acq_rel);
        },
        scratch);

    kb::tests::Require(visited.load(std::memory_order_acquire) == static_cast<std::size_t>(kEntityCount), "Reserved ECS query scratch did not visit all entities");
    kb::tests::Require(scratch.workItems_.size() == kExpectedWorkItems, "Reserved ECS query scratch produced an unexpected work-item count");
    kb::tests::Require(scratch.chunks_.size() == kExpectedWorkItems, "Reserved ECS query scratch produced an unexpected worker chunk count");
    kb::tests::Require(scratch.workItems_.capacity() >= kExpectedWorkItems, "Reserved ECS query scratch did not reserve work-item capacity");
    kb::tests::Require(scratch.chunks_.capacity() >= kExpectedWorkItems, "Reserved ECS query scratch did not reserve worker chunk capacity");

    const std::size_t workItemCapacity = scratch.workItems_.capacity();
    const std::size_t chunkCapacity = scratch.chunks_.capacity();
    visited.store(0U, std::memory_order_release);
    query.ForEachMutableBatchKernel(
        settings,
        [&visited](kb::ecs::MutableQueryBatch<EcsPosition, EcsVelocity>& batch) {
            visited.fetch_add(batch.Count(), std::memory_order_acq_rel);
        },
        scratch);

    kb::tests::Require(visited.load(std::memory_order_acquire) == static_cast<std::size_t>(kEntityCount), "Reused ECS query scratch did not visit all entities");
    kb::tests::Require(scratch.workItems_.capacity() == workItemCapacity, "ECS query scratch grew work-item capacity on reuse");
    kb::tests::Require(scratch.chunks_.capacity() == chunkCapacity, "ECS query scratch grew worker chunk capacity on reuse");
}

void RunTypedEcsQueryDeterministicReductionModeTest() {
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

    ReductionQueryCounters counters;
    query.ForEachBatch(
        kb::ecs::QueryExecutionSettings{
            .maxBatchSize = 32,
            .policy = kb::ecs::QueryExecutionPolicy::ParallelRanges,
            .reductionMode = kb::ecs::QueryReductionMode::Deterministic,
            .workerPool = &pool,
        },
        &CountMovingBatchesReductionMode,
        &counters);

    kb::tests::Require(counters.visited == 512U, "Deterministic reduction ECS query did not visit all entities");
    kb::tests::Require(counters.batches == 16U, "Deterministic reduction ECS query ignored the deterministic batch size");
    kb::tests::Require(counters.maxBatch == 32U, "Deterministic reduction ECS query exceeded configured batch size");
    kb::tests::Require(!counters.workerContextActive, "Deterministic reduction ECS query executed through worker fan-out");
}

void RunTypedEcsQueryPerWorkerReductionScratchTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 32,
    });
    for (int index = 0; index < 4096; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = 1.0F, .y = 0.0F });
        world.Set(entity, EcsVelocity{ .x = 2.0F, .y = 0.0F });
    }

    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 2 } };
    const kb::ecs::QueryExecutionSettings settings{
        .maxBatchSize = 32,
        .policy = kb::ecs::QueryExecutionPolicy::ParallelRanges,
        .reductionMode = kb::ecs::QueryReductionMode::PerWorker,
        .workerPool = &pool,
    };
    kb::ecs::QueryReductionScratch<QueryReductionScratchSlot> scratch;
    scratch.ResetForSettings(settings);
    kb::tests::Require(scratch.SlotCount() == 2U, "Per-worker ECS query reduction scratch did not size to worker count");

    kb::ecs::QueryExecutionSettings singleThreadSettings = settings;
    singleThreadSettings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    kb::ecs::QueryReductionScratch<QueryReductionScratchSlot> singleThreadScratch;
    singleThreadScratch.ResetForSettings(singleThreadSettings);
    kb::tests::Require(singleThreadScratch.SlotCount() == 1U, "Single-thread ECS query reduction scratch kept unused worker slots");

    QueryReductionScratchContext context{ .scratch = &scratch };
    kb::ecs::Query<EcsPosition, EcsVelocity> query = world.CreateQuery<EcsPosition, EcsVelocity>();
    query.ForEachBatch(settings, &CountMovingBatchesReductionScratch, &context);

    const QueryReductionScratchSlot totals = scratch.Reduce(QueryReductionScratchSlot{}, [](QueryReductionScratchSlot& accumulator, const QueryReductionScratchSlot& slot) {
        accumulator.visited += slot.visited;
        accumulator.batches += slot.batches;
        accumulator.maxBatch = std::max(accumulator.maxBatch, slot.maxBatch);
        accumulator.sumX += slot.sumX;
        accumulator.workerMask |= slot.workerMask;
    });

    kb::tests::Require(totals.visited == 4096U, "Per-worker ECS query reduction did not visit all entities");
    kb::tests::Require(totals.sumX == 12288, "Per-worker ECS query reduction produced an invalid sum");
    kb::tests::Require(totals.batches >= 128U, "Per-worker ECS query reduction did not split the workload");
    kb::tests::Require(totals.maxBatch <= 32U, "Per-worker ECS query reduction exceeded configured batch size");
    kb::tests::Require(CountSetBits(totals.workerMask) >= 2U, "Per-worker ECS query reduction did not use multiple worker slots");
}

void RunTypedEcsQueryPerWorkerReductionScratchHonorsWorkerOverrideTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 16,
    });
    for (int index = 0; index < 1024; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = 1.0F, .y = 0.0F });
        world.Set(entity, EcsVelocity{ .x = 2.0F, .y = 0.0F });
    }

    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 4 } };
    const kb::ecs::QueryExecutionSettings settings{
        .maxBatchSize = 16,
        .policy = kb::ecs::QueryExecutionPolicy::ParallelRanges,
        .reductionMode = kb::ecs::QueryReductionMode::PerWorker,
        .workerCountOverride = 2,
        .workerPool = &pool,
    };
    kb::ecs::QueryReductionScratch<QueryReductionScratchSlot> scratch;
    scratch.ResetForSettings(settings);
    kb::tests::Require(scratch.SlotCount() == 2U, "Per-worker ECS query reduction scratch ignored worker override");

    QueryReductionScratchContext context{ .scratch = &scratch };
    kb::ecs::Query<EcsPosition, EcsVelocity> query = world.CreateQuery<EcsPosition, EcsVelocity>();
    query.ForEachBatch(settings, &CountMovingBatchesReductionScratch, &context);

    const QueryReductionScratchSlot totals = scratch.Reduce(QueryReductionScratchSlot{}, [](QueryReductionScratchSlot& accumulator, const QueryReductionScratchSlot& slot) {
        accumulator.visited += slot.visited;
        accumulator.batches += slot.batches;
        accumulator.maxBatch = std::max(accumulator.maxBatch, slot.maxBatch);
        accumulator.sumX += slot.sumX;
        accumulator.workerMask |= slot.workerMask;
    });

    kb::tests::Require(totals.visited == 1024U, "Worker-capped ECS query reduction did not visit all entities");
    kb::tests::Require(totals.sumX == 3072, "Worker-capped ECS query reduction produced an invalid sum");
    kb::tests::Require(totals.maxBatch <= 16U, "Worker-capped ECS query reduction exceeded configured batch size");
    kb::tests::Require((totals.workerMask & ~0b11U) == 0U, "Worker-capped ECS query reduction used a slot outside the override");
    kb::tests::Require(CountSetBits(totals.workerMask) == 2U, "Worker-capped ECS query reduction did not use both allowed worker slots");
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

void RunTypedEcsMutableQueryBatchMatchesReferenceTest() {
    kb::ecs::World batchWorld(kb::ecs::WorldConfig{
        .executionGrainSize = 7,
    });
    kb::ecs::World referenceWorld(kb::ecs::WorldConfig{
        .executionGrainSize = 7,
    });

    std::vector<kb::ecs::Entity> batchEntities;
    std::vector<kb::ecs::Entity> referenceEntities;
    batchEntities.reserve(128);
    referenceEntities.reserve(128);
    for (int index = 0; index < 128; ++index) {
        const EcsPosition position{
            .x = static_cast<float>(index),
            .y = static_cast<float>(index % 11),
        };
        const EcsVelocity velocity{
            .x = static_cast<float>((index % 5) + 1),
            .y = static_cast<float>((index % 7) - 3),
        };

        const kb::ecs::Entity batchEntity = batchWorld.CreateEntity();
        const kb::ecs::Entity referenceEntity = referenceWorld.CreateEntity();
        batchEntities.push_back(batchEntity);
        referenceEntities.push_back(referenceEntity);

        batchWorld.Set(batchEntity, position);
        referenceWorld.Set(referenceEntity, position);
        if ((index % 5) != 0) {
            batchWorld.Set(batchEntity, velocity);
            referenceWorld.Set(referenceEntity, velocity);
        }
        if ((index % 3) == 0) {
            batchWorld.Set(batchEntity, EcsQueryMarker{ .value = index });
            referenceWorld.Set(referenceEntity, EcsQueryMarker{ .value = index });
        }
        if ((index % 8) == 0) {
            batchWorld.Set(batchEntity, EcsDisabled{ .value = 1 });
            referenceWorld.Set(referenceEntity, EcsDisabled{ .value = 1 });
        }
    }

    EcsBatchCounters batchCounters;
    kb::ecs::Query<EcsPosition, EcsVelocity> batchQuery = batchWorld.CreateQuery<EcsPosition, EcsVelocity>();
    batchQuery.ForEachMutableBatch(
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 7 },
        [](kb::ecs::MutableQueryBatch<EcsPosition, EcsVelocity>& batch, void* context) {
            auto* counters = static_cast<EcsBatchCounters*>(context);
            ++counters->batches;
            counters->maxBatch = std::max(counters->maxBatch, batch.Count());

            EcsPosition* positions = batch.Components<0>();
            const EcsVelocity* velocities = batch.Components<1>();
            for (std::size_t row = 0; row < batch.Count(); ++row) {
                kb::tests::Require(batch.EntityAt(row).IsValid(), "Mutable ECS batch reference test returned an invalid entity");
                positions[row].x = (positions[row].x * 1.5F) + velocities[row].x;
                positions[row].y += velocities[row].y * 0.25F;
                ++counters->visited;
            }
        },
        &batchCounters);

    int referenceVisited = 0;
    for (kb::ecs::Entity entity : referenceEntities) {
        if (!referenceWorld.Has<EcsVelocity>(entity)) {
            continue;
        }
        EcsPosition* position = referenceWorld.TryGetMutable<EcsPosition>(entity);
        const EcsVelocity* velocity = referenceWorld.TryGet<EcsVelocity>(entity);
        kb::tests::Require(position != nullptr && velocity != nullptr, "Mutable ECS reference path found an incomplete entity");
        position->x = (position->x * 1.5F) + velocity->x;
        position->y += velocity->y * 0.25F;
        ++referenceVisited;
    }

    kb::tests::Require(batchCounters.visited == referenceVisited, "Mutable ECS batch query visited a different entity count than the reference path");
    kb::tests::Require(batchCounters.batches >= 15, "Mutable ECS batch reference test did not split the workload into small batches");
    kb::tests::Require(batchCounters.maxBatch <= 7, "Mutable ECS batch reference test exceeded configured batch size");

    for (std::size_t index = 0; index < batchEntities.size(); ++index) {
        const EcsPosition* batchPosition = batchWorld.TryGet<EcsPosition>(batchEntities[index]);
        const EcsPosition* referencePosition = referenceWorld.TryGet<EcsPosition>(referenceEntities[index]);
        kb::tests::Require(batchPosition != nullptr && referencePosition != nullptr, "Mutable ECS batch reference comparison lost a position component");
        kb::tests::Require(kb::tests::NearlyEqual(batchPosition->x, referencePosition->x), "Mutable ECS batch query diverged from reference position x");
        kb::tests::Require(kb::tests::NearlyEqual(batchPosition->y, referencePosition->y), "Mutable ECS batch query diverged from reference position y");
    }
}

#if !defined(NDEBUG)
void RunTypedEcsMutableQueryBorrowLockTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 4,
    });

    for (int index = 0; index < 4; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 0.0F });
    }

    kb::ecs::Query<EcsPosition> outerQuery = world.CreateQuery<EcsPosition>();
    kb::ecs::Query<EcsPosition> innerQuery = world.CreateQuery<EcsPosition>();

    bool conflictRejected = false;
    try {
        outerQuery.ForEachMutableBatchKernel(kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 }, [&innerQuery](kb::ecs::MutableQueryBatch<EcsPosition>& batch) {
            EcsPosition* positions = batch.Components<0>();
            positions[0].x += 1.0F;
            innerQuery.ForEachMutableBatchKernel(kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 }, [](kb::ecs::MutableQueryBatch<EcsPosition>& nestedBatch) {
                EcsPosition* nestedPositions = nestedBatch.Components<0>();
                nestedPositions[0].x += 1.0F;
            });
        });
    } catch (const std::logic_error&) {
        conflictRejected = true;
    }

    kb::tests::Require(conflictRejected, "Debug ECS mutable borrow locks allowed overlapping mutable component views");

    bool releasedAfterException = false;
    outerQuery.ForEachMutableBatchKernel(kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 }, [&releasedAfterException](kb::ecs::MutableQueryBatch<EcsPosition>& batch) {
        EcsPosition* positions = batch.Components<0>();
        positions[0].x += 1.0F;
        releasedAfterException = true;
    });
    kb::tests::Require(releasedAfterException, "Debug ECS mutable borrow lock was not released after an exception");
}

void RunTypedEcsParallelMutableQueryBorrowConflictTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 16,
    });

    for (int index = 0; index < 16; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 0.0F });
    }

    kb::ecs::Query<EcsPosition> firstQuery = world.CreateQuery<EcsPosition>();
    kb::ecs::Query<EcsPosition> secondQuery = world.CreateQuery<EcsPosition>();
    std::atomic<bool> firstBatchEntered = false;
    std::atomic<bool> releaseFirstBatch = false;
    std::atomic<bool> firstThreadCompleted = false;

    std::thread firstThread{ [&] {
        firstQuery.ForEachMutableBatchKernel(kb::ecs::QueryExecutionSettings{ .maxBatchSize = 16 }, [&](kb::ecs::MutableQueryBatch<EcsPosition>& batch) {
            firstBatchEntered.store(true, std::memory_order_release);
            while (!releaseFirstBatch.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
            }
            EcsPosition* positions = batch.Components<0>();
            positions[0].x += 1.0F;
        });
        firstThreadCompleted.store(true, std::memory_order_release);
    } };

    while (!firstBatchEntered.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
    }

    bool conflictRejected = false;
    try {
        secondQuery.ForEachMutableBatchKernel(kb::ecs::QueryExecutionSettings{ .maxBatchSize = 16 }, [](kb::ecs::MutableQueryBatch<EcsPosition>& batch) {
            EcsPosition* positions = batch.Components<0>();
            positions[0].x += 1.0F;
        });
    } catch (const std::logic_error&) {
        conflictRejected = true;
    }

    releaseFirstBatch.store(true, std::memory_order_release);
    firstThread.join();

    kb::tests::Require(conflictRejected, "Debug ECS mutable borrow locks allowed parallel mutable query batches to overlap");
    kb::tests::Require(firstThreadCompleted.load(std::memory_order_acquire), "Debug ECS parallel mutable query setup did not release the first batch");
}
#endif

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

void RunTypedEcsQueryRepeatedPlanConsistencyTest() {
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

    kb::tests::Require(firstCounters.visited == 75, "Native ECS query plan did not visit all entities through first query");
    kb::tests::Require(secondCounters.visited == 75, "Native ECS query plan did not visit all entities through second query");
    kb::tests::Require(kb::tests::NearlyEqual(firstCounters.sumX, secondCounters.sumX), "Native ECS query plan produced inconsistent repeated query results");
    kb::tests::Require(kb::tests::NearlyEqual(firstCounters.sumX, 3000.0F), "Native ECS query plan saw invalid component data after structural changes");
}

void RunUnsafeHotQueryMutableRangePlanTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 4,
    });

    std::vector<kb::ecs::Entity> entities;
    entities.reserve(11);
    for (int index = 0; index < 10; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = static_cast<float>(index * 2) });
        world.Set(entity, EcsVelocity{ .x = 10.0F, .y = -2.0F });
        entities.push_back(entity);
    }

    kb::ecs::Query<EcsPosition, EcsVelocity> query = world.CreateQuery<EcsPosition, EcsVelocity>();
    kb::ecs::UnsafeHotReadQuery<EcsPosition, EcsVelocity> readHotQuery;
    kb::tests::Require(readHotQuery.Rebuild(query, kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 }), "ECS unsafe hot read query failed to build a pointer plan");
    kb::tests::Require(readHotQuery.IsValid(), "ECS unsafe hot read query reported invalid after rebuild");
    kb::tests::Require(!readHotQuery.IsStale(query), "ECS unsafe hot read query reported stale immediately after rebuild");
    kb::tests::Require(readHotQuery.CachedStructuralVersion() == query.StructuralVersion(), "ECS unsafe hot read query did not capture the source structural version");
    kb::tests::Require(readHotQuery.EntityCount() == entities.size(), "ECS unsafe hot read query captured an invalid entity count");
    kb::tests::Require(readHotQuery.DefaultRangeSize() == 4U, "ECS unsafe hot read query did not retain the configured default range size");
    kb::tests::Require(readHotQuery.CachedRangeSize() == 4U, "ECS unsafe hot read query did not prebuild the default range plan");
    kb::tests::Require(readHotQuery.CachedRangeCount() == 3U, "ECS unsafe hot read query prebuilt an invalid range count");

    const kb::ecs::WorldTelemetrySnapshot beforeReadExecute = world.TelemetrySnapshot();
    std::size_t readVisited = 0;
    std::size_t readMaxRange = 0;
    float readChecksum = 0.0F;
    const kb::ecs::UnsafeHotRangeDispatchStats readStats = readHotQuery.ForEachKernel(0, [&](kb::ecs::UnsafeHotChunk<EcsPosition, EcsVelocity>& chunk) {
        readMaxRange = std::max(readMaxRange, chunk.Count());
        const EcsPosition* positions = chunk.Components<0>();
        const EcsVelocity* velocities = chunk.Components<1>();
        for (std::size_t row = 0; row < chunk.Count(); ++row) {
            readChecksum += positions[row].x + velocities[row].x;
            ++readVisited;
        }
    });
    const kb::ecs::WorldTelemetrySnapshot afterReadExecute = world.TelemetrySnapshot();
    kb::tests::Require(readVisited == entities.size(), "ECS unsafe hot read query did not visit every row");
    kb::tests::Require(readMaxRange <= 4U, "ECS unsafe hot read query ignored the configured default range size");
    kb::tests::Require(readChecksum > 0.0F, "ECS unsafe hot read query produced an invalid checksum");
    kb::tests::Require(readStats.ranges == 3U, "ECS unsafe hot read query reported an invalid kernel range count");
    kb::tests::Require(readStats.entities == entities.size(), "ECS unsafe hot read query reported an invalid kernel entity count");
    kb::tests::Require(readStats.maxRangeSize <= 4U, "ECS unsafe hot read query reported an invalid max range size");
    kb::tests::Require(readStats.cachedRangeSize == 4U, "ECS unsafe hot read query reported an invalid default dispatch grain");
    kb::tests::Require(readStats.bytesRead == entities.size() * (sizeof(EcsPosition) + sizeof(EcsVelocity)), "ECS unsafe hot read query reported invalid read traffic");
    kb::tests::Require(readStats.bytesWritten == 0U, "ECS unsafe hot read query reported write traffic");
    kb::tests::Require(readStats.bytesTouched == readStats.bytesRead, "ECS unsafe hot read query reported invalid total traffic");
    kb::tests::Require(afterReadExecute.queryExecutions == beforeReadExecute.queryExecutions, "ECS unsafe hot read query went through the safe query executor");

    kb::ecs::UnsafeHotQuery<EcsPosition, EcsVelocity> hotQuery;
    kb::tests::Require(hotQuery.Rebuild(query, kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 }), "ECS unsafe hot query failed to build a mutable pointer plan");
    kb::tests::Require(hotQuery.IsValid(), "ECS unsafe hot query reported invalid after rebuild");
    kb::tests::Require(!hotQuery.IsStale(query), "ECS unsafe hot query reported stale immediately after rebuild");
    kb::tests::Require(hotQuery.CachedStructuralVersion() == query.StructuralVersion(), "ECS unsafe hot query did not capture the source structural version");
    kb::tests::Require(hotQuery.EntityCount() == entities.size(), "ECS unsafe hot query captured an invalid entity count");
    kb::tests::Require(hotQuery.ChunkCount() >= 1U, "ECS unsafe hot query captured no chunks");
    kb::tests::Require(hotQuery.DefaultRangeSize() == 4U, "ECS unsafe hot query did not retain the configured default range size");
    kb::tests::Require(hotQuery.CachedRangeSize() == 4U, "ECS unsafe hot query did not prebuild the default range plan");
    kb::tests::Require(hotQuery.CachedRangeCount() == 3U, "ECS unsafe hot query prebuilt an invalid range count");

    std::size_t defaultRangeMax = 0;
    std::size_t defaultRangeVisited = 0;
    const kb::ecs::UnsafeHotRangeDispatchStats defaultMutableStats = hotQuery.ForEachMutableKernel(0, [&](kb::ecs::UnsafeHotMutableChunk<EcsPosition, EcsVelocity>& chunk) {
        defaultRangeMax = std::max(defaultRangeMax, chunk.Count());
        defaultRangeVisited += chunk.Count();
    });
    kb::tests::Require(defaultRangeVisited == entities.size(), "ECS unsafe hot query default range did not visit all entities");
    kb::tests::Require(defaultRangeMax <= 4U, "ECS unsafe hot query ignored the configured default range size");
    kb::tests::Require(defaultMutableStats.ranges == 3U, "ECS unsafe hot query reported an invalid mutable kernel range count");
    kb::tests::Require(defaultMutableStats.entities == entities.size(), "ECS unsafe hot query reported an invalid mutable kernel entity count");
    kb::tests::Require(defaultMutableStats.cachedRangeSize == 4U, "ECS unsafe hot query reported an invalid mutable kernel dispatch grain");
    kb::tests::Require(defaultMutableStats.bytesRead == entities.size() * (sizeof(EcsPosition) + sizeof(EcsVelocity)), "ECS unsafe hot query reported invalid mutable read traffic");
    kb::tests::Require(defaultMutableStats.bytesWritten == defaultMutableStats.bytesRead, "ECS unsafe hot query reported invalid mutable write traffic");
    kb::tests::Require(defaultMutableStats.bytesTouched == defaultMutableStats.bytesRead + defaultMutableStats.bytesWritten, "ECS unsafe hot query reported invalid mutable total traffic");
    kb::tests::Require(hotQuery.CachedDispatchStats().ranges == 3U, "ECS unsafe hot query cached dispatch stats lost the prebuilt range plan");

    auto& nativeStorage = const_cast<kb::ecs::NativeArchetypeStorage&>(world.NativeStorage());
    const kb::ecs::ComponentId positionComponent = world.Component<EcsPosition>();
    const kb::ecs::ComponentId velocityComponent = world.Component<EcsVelocity>();
    kb::ecs::QueryBatchExecutionScratch dirtyScratch;
    query.PrepareMutableBatchExecution(kb::ecs::QueryExecutionSettings{}, dirtyScratch);
    for (const kb::ecs::MutableQueryTableDispatchRecord& record : dirtyScratch.mutableRecords_) {
        nativeStorage.ClearComponentDirtyRows(record.nativeArchetypeIndex, record.nativeChunkIndex, positionComponent);
        nativeStorage.ClearComponentDirtyRows(record.nativeArchetypeIndex, record.nativeChunkIndex, velocityComponent);
    }
    kb::tests::Require(hotQuery.Rebuild(query, kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 }), "ECS unsafe hot query failed to rebuild after clearing dirty metadata");
    hotQuery.ForEachMutableChunk([](kb::ecs::UnsafeHotMutableChunk<EcsPosition, EcsVelocity>& chunk) {
        kb::tests::Require(chunk.DirtyCount<0>() == 0U, "ECS unsafe hot query exposed stale position dirty metadata");
        kb::tests::Require(chunk.DirtyCount<1>() == 0U, "ECS unsafe hot query exposed stale velocity dirty metadata");
    });
    kb::tests::Require(!dirtyScratch.mutableRecords_.empty(), "ECS unsafe hot query dirty range test did not capture a mutable record");
    const kb::ecs::MutableQueryTableDispatchRecord dirtyRecord = dirtyScratch.mutableRecords_.front();
    nativeStorage.MarkArchetypeChunkComponentsModified(
        dirtyRecord.nativeArchetypeIndex,
        dirtyRecord.nativeChunkIndex,
        0U,
        4U,
        std::span<const kb::ecs::ComponentId>{ &positionComponent, 1U });
    kb::tests::Require(readHotQuery.Rebuild(query, kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 }), "ECS unsafe hot read query failed to rebuild before parallel dirty ranges");
    kb::ecs::WorkerPool readDirtyPool(kb::ecs::WorkerPoolConfig{ .workerCount = 2 });
    std::atomic_size_t readDirtyRangeVisits{ 0U };
    std::atomic_size_t readDirtyRows{ 0U };
    std::atomic_size_t readDirtyChecksum{ 0U };
    const kb::ecs::UnsafeHotDirtyRangeDispatchStats readDirtyStats = readHotQuery.ForEachDirtyRangeParallel<0>(
        nativeStorage,
        2U,
        readDirtyPool,
        2U,
        [&](const kb::ecs::UnsafeHotChunk<EcsPosition, EcsVelocity>& chunk, std::size_t dirtyCount, kb::ecs::WorkerContext workerContext) {
            static_cast<void>(workerContext);
            readDirtyRangeVisits.fetch_add(1U, std::memory_order_relaxed);
            readDirtyRows.fetch_add(dirtyCount, std::memory_order_relaxed);
            kb::tests::Require(chunk.Count() <= 2U, "ECS unsafe hot parallel read dirty range exceeded requested range size");
            const EcsPosition* positions = chunk.Components<0>();
            for (std::size_t row = 0; row < chunk.Count(); ++row) {
                readDirtyChecksum.fetch_add(static_cast<std::size_t>(positions[row].x), std::memory_order_relaxed);
            }
        });
    kb::tests::Require(readDirtyStats.ranges == 2U, "ECS unsafe hot parallel read dirty query built an invalid range count");
    kb::tests::Require(readDirtyStats.chunks == 1U, "ECS unsafe hot parallel read dirty query reported an invalid chunk count");
    kb::tests::Require(readDirtyStats.entities == 4U, "ECS unsafe hot parallel read dirty query reported an invalid entity count");
    kb::tests::Require(readDirtyStats.dirtyRows == 4U, "ECS unsafe hot parallel read dirty query reported an invalid dirty row count");
    kb::tests::Require(readDirtyStats.maxRangeSize == 2U, "ECS unsafe hot parallel read dirty query reported an invalid max range size");
    kb::tests::Require(readDirtyStats.requestedRangeSize == 2U, "ECS unsafe hot parallel read dirty query lost the requested range size");
    kb::tests::Require(readDirtyStats.workerCountLimit == 2U, "ECS unsafe hot parallel read dirty query lost the worker cap");
    kb::tests::Require(readDirtyStats.bytesRead == 4U * (sizeof(EcsPosition) + sizeof(EcsVelocity)), "ECS unsafe hot parallel read dirty query reported invalid read traffic");
    kb::tests::Require(readDirtyStats.bytesWritten == 0U, "ECS unsafe hot parallel read dirty query reported write traffic");
    kb::tests::Require(readDirtyStats.bytesTouched == readDirtyStats.bytesRead, "ECS unsafe hot parallel read dirty query reported invalid touched traffic");
    kb::tests::Require(readDirtyRangeVisits.load(std::memory_order_relaxed) == 2U, "ECS unsafe hot parallel read dirty query visited an invalid range count");
    kb::tests::Require(readDirtyRows.load(std::memory_order_relaxed) == 4U, "ECS unsafe hot parallel read dirty query visited an invalid dirty row count");
    kb::tests::Require(readDirtyChecksum.load(std::memory_order_relaxed) == 6U, "ECS unsafe hot parallel read dirty query read invalid data");
    kb::tests::Require(
        nativeStorage.ComponentDirtyCount(dirtyRecord.nativeArchetypeIndex, dirtyRecord.nativeChunkIndex, positionComponent) == 4U,
        "ECS unsafe hot parallel read dirty query unexpectedly cleared dirty rows");
    nativeStorage.ClearComponentDirtyRows(dirtyRecord.nativeArchetypeIndex, dirtyRecord.nativeChunkIndex, positionComponent);

    nativeStorage.MarkArchetypeChunkComponentsModified(
        dirtyRecord.nativeArchetypeIndex,
        dirtyRecord.nativeChunkIndex,
        2U,
        2U,
        std::span<const kb::ecs::ComponentId>{ &positionComponent, 1U });
    kb::tests::Require(hotQuery.RebuildIfChanged(query), "ECS unsafe hot query failed to refresh dirty metadata without structural changes");
    hotQuery.ForEachMutableChunk([](kb::ecs::UnsafeHotMutableChunk<EcsPosition, EcsVelocity>& chunk) {
        kb::tests::Require(chunk.DirtyCount<0>() == 2U, "ECS unsafe hot query conditional rebuild did not refresh dirty metadata");
    });
    std::vector<kb::ecs::NativeComponentDirtyRange> dirtyRanges;
    std::size_t dirtyRangeVisits = 0U;
    std::size_t dirtyRows = 0U;
    const kb::ecs::UnsafeHotDirtyRangeDispatchStats dirtyStats = hotQuery.ForEachDirtyMutableRange<0>(
        nativeStorage,
        2U,
        dirtyRanges,
        true,
        [&](kb::ecs::UnsafeHotMutableChunk<EcsPosition, EcsVelocity>& chunk, std::size_t dirtyCount) {
            ++dirtyRangeVisits;
            dirtyRows += dirtyCount;
            kb::tests::Require(chunk.Count() == 2U, "ECS unsafe hot dirty range did not preserve requested range size");
            EcsPosition* positions = chunk.Components<0>();
            for (std::size_t row = 0; row < chunk.Count(); ++row) {
                positions[row].y += 100.0F;
            }
        });
    kb::tests::Require(dirtyStats.ranges == 1U, "ECS unsafe hot dirty query reported an invalid range count");
    kb::tests::Require(dirtyStats.chunks == 1U, "ECS unsafe hot dirty query reported an invalid chunk count");
    kb::tests::Require(dirtyStats.entities == 2U, "ECS unsafe hot dirty query reported an invalid entity count");
    kb::tests::Require(dirtyStats.dirtyRows == 2U, "ECS unsafe hot dirty query reported an invalid dirty stats count");
    kb::tests::Require(dirtyStats.maxRangeSize == 2U, "ECS unsafe hot dirty query reported an invalid max range size");
    kb::tests::Require(dirtyStats.bytesRead == 2U * (sizeof(EcsPosition) + sizeof(EcsVelocity)), "ECS unsafe hot dirty query reported invalid read traffic");
    kb::tests::Require(dirtyStats.bytesWritten == 2U * (sizeof(EcsPosition) + sizeof(EcsVelocity)), "ECS unsafe hot dirty query reported invalid write traffic");
    kb::tests::Require(dirtyStats.bytesTouched == dirtyStats.bytesRead + dirtyStats.bytesWritten, "ECS unsafe hot dirty query reported invalid touched traffic");
    kb::tests::Require(dirtyRangeVisits == 1U, "ECS unsafe hot dirty query visited an invalid range count");
    kb::tests::Require(dirtyRows == 2U, "ECS unsafe hot dirty query reported an invalid dirty row count");
    kb::tests::Require(
        nativeStorage.ComponentDirtyCount(dirtyRecord.nativeArchetypeIndex, dirtyRecord.nativeChunkIndex, positionComponent) == 0U,
        "ECS unsafe hot dirty query did not clear visited dirty rows");
    kb::tests::Require(kb::tests::NearlyEqual(world.TryGet<EcsPosition>(entities[2])->y, 104.0F), "ECS unsafe hot dirty query did not mutate the first dirty row");
    kb::tests::Require(kb::tests::NearlyEqual(world.TryGet<EcsPosition>(entities[3])->y, 106.0F), "ECS unsafe hot dirty query did not mutate the second dirty row");

    nativeStorage.MarkArchetypeChunkComponentsModified(
        dirtyRecord.nativeArchetypeIndex,
        dirtyRecord.nativeChunkIndex,
        4U,
        1U,
        std::span<const kb::ecs::ComponentId>{ &positionComponent, 1U });
    std::size_t reusedDirtyRangeVisits = 0U;
    const kb::ecs::UnsafeHotDirtyRangeDispatchStats reusedDirtyStats = hotQuery.ForEachDirtyMutableRange<0>(
        nativeStorage,
        2U,
        dirtyRanges,
        true,
        [&](kb::ecs::UnsafeHotMutableChunk<EcsPosition, EcsVelocity>& chunk, std::size_t dirtyCount) {
            ++reusedDirtyRangeVisits;
            kb::tests::Require(dirtyCount == 1U, "ECS unsafe hot dirty query reused plan reported an invalid dirty count");
            EcsPosition* positions = chunk.Components<0>();
            for (std::size_t row = 0; row < chunk.Count(); ++row) {
                positions[row].x += 300.0F;
            }
        });
    kb::tests::Require(reusedDirtyStats.ranges == 1U, "ECS unsafe hot dirty query reused plan skipped fresh dirty metadata");
    kb::tests::Require(reusedDirtyStats.dirtyRows == 1U, "ECS unsafe hot dirty query reused plan reported invalid dirty rows");
    kb::tests::Require(reusedDirtyRangeVisits == 1U, "ECS unsafe hot dirty query reused plan visited an invalid range count");

    nativeStorage.MarkArchetypeChunkComponentsModified(
        dirtyRecord.nativeArchetypeIndex,
        dirtyRecord.nativeChunkIndex,
        6U,
        4U,
        std::span<const kb::ecs::ComponentId>{ &positionComponent, 1U });
    kb::tests::Require(hotQuery.Rebuild(query, kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 }), "ECS unsafe hot query failed to rebuild before parallel dirty ranges");
    kb::ecs::WorkerPool dirtyPool(kb::ecs::WorkerPoolConfig{ .workerCount = 2 });
    std::atomic_size_t parallelDirtyRangeVisits{ 0U };
    std::atomic_size_t parallelDirtyRows{ 0U };
    const kb::ecs::UnsafeHotDirtyRangeDispatchStats parallelDirtyStats = hotQuery.ForEachDirtyMutableRangeParallel<0>(
        nativeStorage,
        2U,
        dirtyPool,
        2U,
        true,
        [&](kb::ecs::UnsafeHotMutableChunk<EcsPosition, EcsVelocity>& chunk, std::size_t dirtyCount, kb::ecs::WorkerContext workerContext) {
            static_cast<void>(workerContext);
            parallelDirtyRangeVisits.fetch_add(1U, std::memory_order_relaxed);
            parallelDirtyRows.fetch_add(dirtyCount, std::memory_order_relaxed);
            kb::tests::Require(chunk.Count() <= 2U, "ECS unsafe hot parallel dirty range exceeded requested range size");
            EcsPosition* positions = chunk.Components<0>();
            for (std::size_t row = 0; row < chunk.Count(); ++row) {
                positions[row].x += 200.0F;
            }
        });
    kb::tests::Require(parallelDirtyStats.ranges == 2U, "ECS unsafe hot parallel dirty query built an invalid range count");
    kb::tests::Require(parallelDirtyStats.chunks == 1U, "ECS unsafe hot parallel dirty query reported an invalid chunk count");
    kb::tests::Require(parallelDirtyStats.entities == 4U, "ECS unsafe hot parallel dirty query reported an invalid entity count");
    kb::tests::Require(parallelDirtyStats.dirtyRows == 4U, "ECS unsafe hot parallel dirty query reported an invalid dirty row count");
    kb::tests::Require(parallelDirtyStats.maxRangeSize == 2U, "ECS unsafe hot parallel dirty query reported an invalid max range size");
    kb::tests::Require(parallelDirtyStats.requestedRangeSize == 2U, "ECS unsafe hot parallel dirty query lost the requested range size");
    kb::tests::Require(parallelDirtyStats.workerCountLimit == 2U, "ECS unsafe hot parallel dirty query lost the worker cap");
    kb::tests::Require(parallelDirtyStats.bytesRead == 4U * (sizeof(EcsPosition) + sizeof(EcsVelocity)), "ECS unsafe hot parallel dirty query reported invalid read traffic");
    kb::tests::Require(parallelDirtyStats.bytesWritten == 4U * (sizeof(EcsPosition) + sizeof(EcsVelocity)), "ECS unsafe hot parallel dirty query reported invalid write traffic");
    kb::tests::Require(parallelDirtyStats.bytesTouched == parallelDirtyStats.bytesRead + parallelDirtyStats.bytesWritten, "ECS unsafe hot parallel dirty query reported invalid touched traffic");
    kb::tests::Require(parallelDirtyRangeVisits.load(std::memory_order_relaxed) == 2U, "ECS unsafe hot parallel dirty query visited an invalid range count");
    kb::tests::Require(parallelDirtyRows.load(std::memory_order_relaxed) == 4U, "ECS unsafe hot parallel dirty query visited an invalid dirty row count");
    kb::tests::Require(
        nativeStorage.ComponentDirtyCount(dirtyRecord.nativeArchetypeIndex, dirtyRecord.nativeChunkIndex, positionComponent) == 0U,
        "ECS unsafe hot parallel dirty query did not clear visited dirty rows");
    for (std::size_t index = 6U; index < 10U; ++index) {
        kb::tests::Require(kb::tests::NearlyEqual(world.TryGet<EcsPosition>(entities[index])->x, static_cast<float>(index) + 200.0F), "ECS unsafe hot parallel dirty query did not mutate a dirty row");
    }

    const kb::ecs::WorldTelemetrySnapshot beforeExecute = world.TelemetrySnapshot();
    std::size_t visited = 0;
    std::size_t ranges = 0;
    std::size_t maxRange = 0;
    const kb::ecs::UnsafeHotRangeDispatchStats executeStats = hotQuery.ForEachMutableKernel(3, [&](kb::ecs::UnsafeHotMutableChunk<EcsPosition, EcsVelocity>& chunk) {
        ++ranges;
        maxRange = std::max(maxRange, chunk.Count());
        EcsPosition* positions = chunk.Components<0>();
        const EcsVelocity* velocities = chunk.Components<1>();
        for (std::size_t row = 0; row < chunk.Count(); ++row) {
            kb::tests::Require(chunk.EntityAt(row).IsValid(), "ECS unsafe hot query returned an invalid entity in a range");
            positions[row].x += velocities[row].x * 0.5F;
            positions[row].y += velocities[row].y * 0.5F;
            ++visited;
        }
    });
    hotQuery.MarkCachedRangesDirty(nativeStorage);

    const kb::ecs::WorldTelemetrySnapshot afterExecute = world.TelemetrySnapshot();
    kb::tests::Require(visited == entities.size(), "ECS unsafe hot query did not visit every row");
    kb::tests::Require(ranges >= 4U, "ECS unsafe hot query did not split ranges by requested grain");
    kb::tests::Require(maxRange <= 3U, "ECS unsafe hot query exceeded requested range size");
    kb::tests::Require(executeStats.entities == entities.size(), "ECS unsafe hot query reported an invalid explicit kernel entity count");
    kb::tests::Require(executeStats.ranges == ranges, "ECS unsafe hot query reported an invalid explicit kernel range count");
    kb::tests::Require(executeStats.maxRangeSize == maxRange, "ECS unsafe hot query reported an invalid explicit max range size");
    kb::tests::Require(executeStats.cachedRangeSize == 3U, "ECS unsafe hot query reported an invalid explicit dispatch grain");
    kb::tests::Require(afterExecute.queryPlanRequests == beforeExecute.queryPlanRequests, "ECS unsafe hot query execution performed query discovery");
    kb::tests::Require(afterExecute.queryExecutions == beforeExecute.queryExecutions, "ECS unsafe hot query execution went through the safe query executor");
    kb::tests::Require(hotQuery.Rebuild(query, kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 }), "ECS unsafe hot query failed to rebuild after marking dirty metadata");
    hotQuery.ForEachMutableChunk([expectedDirty = entities.size()](kb::ecs::UnsafeHotMutableChunk<EcsPosition, EcsVelocity>& chunk) {
        kb::tests::Require(chunk.DirtyCount<0>() == expectedDirty, "ECS unsafe hot query did not publish explicit dirty metadata for positions");
        kb::tests::Require(chunk.DirtyCount<1>() == expectedDirty, "ECS unsafe hot query did not publish explicit dirty metadata for velocities");
    });

    for (std::size_t index = 0; index < entities.size(); ++index) {
        const EcsPosition* position = world.TryGet<EcsPosition>(entities[index]);
        kb::tests::Require(position != nullptr, "ECS unsafe hot query update lost a position component");
        const float expectedX = static_cast<float>(index) + 5.0F + (index == 4U || index == 5U ? 300.0F : 0.0F) + (index >= 6U ? 200.0F : 0.0F);
        kb::tests::Require(kb::tests::NearlyEqual(position->x, expectedX), "ECS unsafe hot query wrote an invalid X value");
        const float expectedY = static_cast<float>(index * 2) - 1.0F + (index == 2U || index == 3U ? 100.0F : 0.0F);
        kb::tests::Require(kb::tests::NearlyEqual(position->y, expectedY), "ECS unsafe hot query wrote an invalid Y value");
    }

    const kb::ecs::Entity lateEntity = world.CreateEntity();
    world.Set(lateEntity, EcsPosition{ .x = 100.0F, .y = 200.0F });
    world.Set(lateEntity, EcsVelocity{ .x = 1.0F, .y = 2.0F });
    entities.push_back(lateEntity);
    kb::tests::Require(hotQuery.IsStale(query), "ECS unsafe hot query did not detect a stale pointer plan after structural changes");
    kb::tests::Require(readHotQuery.IsStale(query), "ECS unsafe hot read query did not detect a stale pointer plan after structural changes");
    kb::tests::Require(hotQuery.RebuildIfChanged(query), "ECS unsafe hot query failed to rebuild after structural changes");
    kb::tests::Require(!hotQuery.IsStale(query), "ECS unsafe hot query stayed stale after conditional rebuild");
    kb::tests::Require(hotQuery.CachedStructuralVersion() == query.StructuralVersion(), "ECS unsafe hot query did not refresh its structural version after conditional rebuild");
    kb::tests::Require(hotQuery.EntityCount() == entities.size(), "ECS unsafe hot query rebuild did not capture a new matching entity");
    kb::tests::Require(hotQuery.CachedRangeSize() == 4U, "ECS unsafe hot query rebuild did not restore the default cached range size");
    kb::tests::Require(hotQuery.CachedRangeCount() == 3U, "ECS unsafe hot query rebuild prebuilt an invalid range count");
    kb::tests::Require(readHotQuery.RebuildIfChanged(query), "ECS unsafe hot read query failed conditional rebuild after structural changes");
    kb::tests::Require(!readHotQuery.IsStale(query), "ECS unsafe hot read query stayed stale after conditional rebuild");

    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 4 } };
    std::atomic<std::uint32_t> workerMask = 0;
    std::atomic<std::uint32_t> invalidWorkerContext = 0;
    std::atomic<std::size_t> parallelVisited = 0;
    const kb::ecs::UnsafeHotRangeDispatchStats parallelStats = hotQuery.ForEachMutableKernelParallel(
        2,
        pool,
        2,
        [&](kb::ecs::UnsafeHotMutableChunk<EcsPosition, EcsVelocity>& chunk, kb::ecs::WorkerContext workerContext) {
            if (workerContext.workerIndex >= 2U || workerContext.workerCount > 2U) {
                invalidWorkerContext.fetch_add(1U, std::memory_order_acq_rel);
            }
            if (workerContext.workerIndex < 32U) {
                workerMask.fetch_or(1U << workerContext.workerIndex, std::memory_order_acq_rel);
            }
            EcsPosition* positions = chunk.Components<0>();
            for (std::size_t row = 0; row < chunk.Count(); ++row) {
                positions[row].y += 1.0F;
            }
            parallelVisited.fetch_add(chunk.Count(), std::memory_order_acq_rel);
        });
    kb::tests::Require(parallelVisited.load(std::memory_order_acquire) == entities.size(), "ECS unsafe hot parallel query did not visit all entities");
    kb::tests::Require(parallelStats.entities == entities.size(), "ECS unsafe hot parallel query reported an invalid entity count");
    kb::tests::Require(parallelStats.ranges == 6U, "ECS unsafe hot parallel query reported an invalid range count");
    kb::tests::Require(parallelStats.maxRangeSize <= 2U, "ECS unsafe hot parallel query reported an invalid max range size");
    kb::tests::Require(parallelStats.workerCountLimit == 2U, "ECS unsafe hot parallel query did not report the worker cap");
    kb::tests::Require(parallelStats.bytesTouched == entities.size() * 2U * (sizeof(EcsPosition) + sizeof(EcsVelocity)), "ECS unsafe hot parallel query reported invalid memory traffic");
    kb::tests::Require(invalidWorkerContext.load(std::memory_order_acquire) == 0U, "ECS unsafe hot parallel query ignored the worker cap");
    kb::tests::Require((workerMask.load(std::memory_order_acquire) & ~0b11U) == 0U, "ECS unsafe hot parallel query used a worker outside the cap");
    kb::tests::Require(hotQuery.CachedRangeSize() == 2U, "ECS unsafe hot parallel query did not cache the requested range size");
    kb::tests::Require(hotQuery.CachedRangeCount() == 6U, "ECS unsafe hot parallel query cached an invalid requested range count");

    bool lateVisited = false;
    hotQuery.ForEachMutableChunk([&](kb::ecs::UnsafeHotMutableChunk<EcsPosition, EcsVelocity>& chunk) {
        EcsPosition* positions = chunk.Components<0>();
        for (std::size_t row = 0; row < chunk.Count(); ++row) {
            if (chunk.EntityAt(row) == lateEntity) {
                positions[row].x += 7.0F;
                lateVisited = true;
            }
        }
    });

    const EcsPosition* latePosition = world.TryGet<EcsPosition>(lateEntity);
    kb::tests::Require(lateVisited && latePosition != nullptr, "ECS unsafe hot query rebuild did not visit the new entity");
    kb::tests::Require(kb::tests::NearlyEqual(latePosition->x, 107.0F), "ECS unsafe hot query rebuild wrote an invalid new entity value");
}

void RunUnsafeHotQueryAdaptiveRangePlanTest() {
    constexpr int kEntityCount = 20'000;
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk512KB;
    config.executionGrainSize = 32;

    kb::ecs::World world(config);
    for (int index = 0; index < kEntityCount; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = 1.0F, .y = 0.0F });
        world.Set(entity, EcsVelocity{ .x = 2.0F, .y = 0.0F });
    }

    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 4 } };
    kb::ecs::Query<EcsPosition, EcsVelocity> query = world.CreateQuery<EcsPosition, EcsVelocity>();
    const kb::ecs::QueryExecutionSettings adaptiveSettings{
        .maxBatchSize = 32,
        .policy = kb::ecs::QueryExecutionPolicy::SIMDPreferred,
        .workerPool = &pool,
        .adaptiveGrain = true,
    };

    kb::ecs::UnsafeHotReadQuery<EcsPosition, EcsVelocity> readHotQuery;
    kb::tests::Require(readHotQuery.Rebuild(query, adaptiveSettings), "ECS unsafe hot read query failed to build an adaptive range plan");
    kb::tests::Require(readHotQuery.DefaultRangeSize() >= 8192U, "ECS unsafe hot read query did not adapt the default range size");
    kb::tests::Require(readHotQuery.CachedRangeSize() == readHotQuery.DefaultRangeSize(), "ECS unsafe hot read query cached a range size different from its adaptive default");
    kb::tests::Require(readHotQuery.CachedRangeCount() <= 3U, "ECS unsafe hot read query did not reduce cached range count");

    std::size_t readVisited = 0U;
    const kb::ecs::UnsafeHotRangeDispatchStats readStats = readHotQuery.ForEachKernel(0, [&](const kb::ecs::UnsafeHotChunk<EcsPosition, EcsVelocity>& chunk) {
        readVisited += chunk.Count();
    });
    kb::tests::Require(readVisited == static_cast<std::size_t>(kEntityCount), "ECS unsafe hot adaptive read query did not visit all entities");
    kb::tests::Require(readStats.cachedRangeSize == readHotQuery.DefaultRangeSize(), "ECS unsafe hot adaptive read query reported an invalid dispatch grain");
    kb::tests::Require(readStats.ranges == readHotQuery.CachedRangeCount(), "ECS unsafe hot adaptive read query reported an invalid range count");

    std::size_t explicitVisited = 0U;
    const kb::ecs::UnsafeHotRangeDispatchStats explicitStats = readHotQuery.ForEachKernel(64, [&](const kb::ecs::UnsafeHotChunk<EcsPosition, EcsVelocity>& chunk) {
        explicitVisited += chunk.Count();
    });
    kb::tests::Require(explicitVisited == static_cast<std::size_t>(kEntityCount), "ECS unsafe hot explicit read query did not visit all entities");
    kb::tests::Require(explicitStats.cachedRangeSize == 64U, "ECS unsafe hot explicit read query ignored requested range size");
    kb::tests::Require(explicitStats.maxRangeSize <= 64U, "ECS unsafe hot explicit read query exceeded requested range size");

    kb::ecs::UnsafeHotQuery<EcsPosition, EcsVelocity> mutableHotQuery;
    kb::tests::Require(mutableHotQuery.Rebuild(query, adaptiveSettings), "ECS unsafe hot mutable query failed to build an adaptive range plan");
    kb::tests::Require(
        mutableHotQuery.DefaultRangeSize() == static_cast<std::size_t>(kEntityCount),
        "ECS unsafe hot mutable query did not coalesce a light linear write");
    kb::tests::Require(mutableHotQuery.CachedRangeSize() == static_cast<std::size_t>(kEntityCount), "ECS unsafe hot mutable query cached an invalid adaptive range size");
    kb::tests::Require(mutableHotQuery.CachedRangeCount() == 1U, "ECS unsafe hot mutable query did not reduce scheduler work to one cached range");
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
        "Fresh ECS query did not match existing query after structural changes");
}

void RunTypedEcsQueryPrepareCacheTracksStorageVersionTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 8,
    });

    std::vector<kb::ecs::Entity> entities;
    entities.reserve(16);
    for (int index = 0; index < 8; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 0.0F });
        world.Set(entity, EcsVelocity{ .x = 1.0F, .y = 0.0F });
        entities.push_back(entity);
    }

    kb::ecs::Query<EcsPosition, EcsVelocity> query = world.CreateQuery<EcsPosition, EcsVelocity>();
    kb::ecs::QueryBatchExecutionScratch scratch;
    constexpr kb::ecs::QueryExecutionSettings telemetrySettings{
        .telemetryEnabled = true,
    };
    query.PrepareBatchExecution(telemetrySettings, scratch);
    kb::ecs::WorldTelemetrySnapshot telemetry = world.TelemetrySnapshot();
    kb::tests::Require(telemetry.queryRecordCacheMisses == 1U, "ECS query record cache did not count the initial prepare miss");
    kb::tests::Require(telemetry.queryRecordCacheHits == 0U, "ECS query record cache counted a hit before reuse");
    const auto countPreparedEntities = [&scratch] {
        std::size_t count = 0;
        for (const kb::ecs::QueryTableDispatchRecord& record : scratch.records_) {
            count += record.entityCount;
        }
        return count;
    };
    kb::tests::Require(countPreparedEntities() == entities.size(), "ECS query prepare cache setup returned an invalid entity count");

    auto& nativeStorage = const_cast<kb::ecs::NativeArchetypeStorage&>(world.NativeStorage());
    const kb::ecs::ComponentId positionComponent = world.Component<EcsPosition>();
    const kb::ecs::ComponentId velocityComponent = world.Component<EcsVelocity>();
    for (const kb::ecs::QueryTableDispatchRecord& record : scratch.records_) {
        nativeStorage.ClearComponentDirtyRows(record.nativeArchetypeIndex, record.nativeChunkIndex, positionComponent);
        nativeStorage.ClearComponentDirtyRows(record.nativeArchetypeIndex, record.nativeChunkIndex, velocityComponent);
    }

    const std::uint64_t stableStructuralVersion = nativeStorage.StructuralVersion();
    query.PrepareBatchExecution(telemetrySettings, scratch);
    telemetry = world.TelemetrySnapshot();
    kb::tests::Require(telemetry.queryRecordCacheHits == 1U, "ECS query record cache did not count stable-version reuse");
    kb::tests::Require(telemetry.queryRecordCacheMisses == 1U, "ECS query record cache reported an extra miss without structural change");
    kb::tests::Require(kb::tests::NearlyEqual(static_cast<float>(telemetry.queryRecordCacheHitPercent), 50.0F), "ECS query record cache hit percent was invalid after reuse");
    kb::tests::Require(kb::tests::NearlyEqual(static_cast<float>(telemetry.queryRecordCacheMissPercent), 50.0F), "ECS query record cache miss percent was invalid after reuse");
    kb::tests::Require(nativeStorage.StructuralVersion() == stableStructuralVersion, "ECS query prepare cache changed storage structure during prepare");
    std::size_t dirtyAfterClear = 0;
    for (const kb::ecs::QueryTableDispatchRecord& record : scratch.records_) {
        dirtyAfterClear += record.componentDirtyCounts[0];
    }
    kb::tests::Require(dirtyAfterClear == 0U, "ECS query prepare cache reused stale dirty metadata after clear");

    world.Set(entities.front(), EcsPosition{ .x = 100.0F, .y = 0.0F });
    kb::tests::Require(nativeStorage.StructuralVersion() == stableStructuralVersion, "ECS query prepare cache test component write changed structural version");
    query.PrepareBatchExecution(telemetrySettings, scratch);
    telemetry = world.TelemetrySnapshot();
    kb::tests::Require(telemetry.queryRecordCacheHits == 2U, "ECS query record cache missed after non-structural component write");
    kb::tests::Require(telemetry.queryRecordCacheMisses == 1U, "ECS query record cache reported a structural miss for component write");
    std::size_t dirtyAfterWrite = 0;
    for (const kb::ecs::QueryTableDispatchRecord& record : scratch.records_) {
        dirtyAfterWrite += record.componentDirtyCounts[0];
    }
    kb::tests::Require(dirtyAfterWrite > 0U, "ECS query prepare cache reused stale dirty metadata after component write");

    const kb::ecs::Entity createdAfterCache = world.CreateEntity();
    world.Set(createdAfterCache, EcsPosition{ .x = 200.0F, .y = 0.0F });
    world.Set(createdAfterCache, EcsVelocity{ .x = 1.0F, .y = 0.0F });
    query.PrepareBatchExecution(telemetrySettings, scratch);
    telemetry = world.TelemetrySnapshot();
    kb::tests::Require(telemetry.queryRecordCacheHits == 2U, "ECS query record cache counted a hit during structural refresh");
    kb::tests::Require(telemetry.queryRecordCacheMisses == 2U, "ECS query record cache did not miss after structural create");
    kb::tests::Require(countPreparedEntities() == entities.size() + 1U, "ECS query prepare cache did not refresh after structural create");
}

void RunTypedEcsQueryCacheChurnStressTest() {
    kb::ecs::World cachedWorld(kb::ecs::WorldConfig{
        .executionGrainSize = 7,
    });
    kb::ecs::World referenceWorld(kb::ecs::WorldConfig{
        .executionGrainSize = 7,
    });

    const kb::ecs::ComponentId cachedMarkerId = cachedWorld.RegisterComponent<EcsQueryMarker>();
    const kb::ecs::ComponentId cachedDisabledId = cachedWorld.RegisterComponent<EcsDisabled>();

    std::vector<kb::ecs::Entity> cachedEntities;
    std::vector<kb::ecs::Entity> referenceEntities;
    cachedEntities.reserve(96);
    referenceEntities.reserve(96);
    for (int index = 0; index < 64; ++index) {
        const kb::ecs::Entity cached = cachedWorld.CreateEntity();
        const kb::ecs::Entity reference = referenceWorld.CreateEntity();
        cachedEntities.push_back(cached);
        referenceEntities.push_back(reference);

        const EcsPosition position{ .x = static_cast<float>(index), .y = 0.0F };
        const EcsVelocity velocity{ .x = static_cast<float>((index % 5) + 1), .y = 0.0F };
        cachedWorld.Set(cached, position);
        referenceWorld.Set(reference, position);
        if ((index % 3) != 0) {
            cachedWorld.Set(cached, velocity);
            referenceWorld.Set(reference, velocity);
        }
        if ((index % 2) == 0) {
            cachedWorld.Set(cached, EcsQueryMarker{ .value = 1 });
            referenceWorld.Set(reference, EcsQueryMarker{ .value = 1 });
        }
        if ((index % 7) == 0) {
            cachedWorld.Set(cached, EcsDisabled{ .value = 1 });
            referenceWorld.Set(reference, EcsDisabled{ .value = 1 });
        }
    }

    kb::ecs::QueryFilter cachedFilter;
    cachedFilter.Require(cachedMarkerId).Exclude(cachedDisabledId);
    kb::ecs::Query<EcsPosition, EcsVelocity> cachedQuery = cachedWorld.CreateQuery<EcsPosition, EcsVelocity>(cachedFilter);

    auto applyChurn = [](kb::ecs::World& world, std::vector<kb::ecs::Entity>& entities, std::uint32_t round) {
        for (std::size_t index = 0; index < entities.size(); ++index) {
            const kb::ecs::Entity entity = entities[index];
            if (!world.IsAlive(entity)) {
                continue;
            }
            if (((index + round) % 4U) == 0U) {
                if (world.Has<EcsQueryMarker>(entity)) {
                    world.Remove<EcsQueryMarker>(entity);
                } else {
                    world.Set(entity, EcsQueryMarker{ .value = static_cast<int>(round + 1U) });
                }
            }
            if (((index * 3U + round) % 9U) == 0U) {
                if (world.Has<EcsDisabled>(entity)) {
                    world.Remove<EcsDisabled>(entity);
                } else {
                    world.Set(entity, EcsDisabled{ .value = 1 });
                }
            }
            if (((index + round) % 6U) == 0U) {
                if (world.Has<EcsVelocity>(entity)) {
                    world.Remove<EcsVelocity>(entity);
                } else {
                    world.Set(entity, EcsVelocity{ .x = static_cast<float>((round % 5U) + 1U), .y = 0.0F });
                }
            }
            if (((index + round) % 17U) == 0U) {
                world.Set(entity, EcsPosition{ .x = static_cast<float>(round + index), .y = 0.0F });
            }
        }

        if ((round % 3U) == 0U && !entities.empty()) {
            const std::size_t victimIndex = round % entities.size();
            if (world.IsAlive(entities[victimIndex])) {
                world.DestroyEntity(entities[victimIndex]);
            }
        }

        if ((round % 2U) == 0U) {
            const kb::ecs::Entity entity = world.CreateEntity();
            world.Set(entity, EcsPosition{ .x = static_cast<float>(1000U + round), .y = 0.0F });
            world.Set(entity, EcsVelocity{ .x = static_cast<float>((round % 7U) + 1U), .y = 0.0F });
            if ((round % 4U) != 0U) {
                world.Set(entity, EcsQueryMarker{ .value = 1 });
            }
            if ((round % 8U) == 0U) {
                world.Set(entity, EcsDisabled{ .value = 1 });
            }
            entities.push_back(entity);
        }
    };

    for (std::uint32_t round = 0; round < 24U; ++round) {
        applyChurn(cachedWorld, cachedEntities, round);
        applyChurn(referenceWorld, referenceEntities, round);

        MovingQuerySnapshot cachedSnapshot = CollectFilteredMovingSnapshot(cachedQuery);
        MovingQuerySnapshot referenceSnapshot = BuildFilteredMovingReference(referenceWorld, referenceEntities, round);
        kb::tests::Require(cachedSnapshot.entityIds.size() == referenceSnapshot.entityIds.size(), "Cached ECS query returned a stale entity count after churn");
        kb::tests::Require(kb::tests::NearlyEqual(cachedSnapshot.sumX, referenceSnapshot.sumX), "Cached ECS query returned stale mutable component values after churn");

        kb::ecs::QueryFilter freshFilter;
        freshFilter.Require(cachedMarkerId).Exclude(cachedDisabledId);
        kb::ecs::Query<EcsPosition, EcsVelocity> freshQuery = cachedWorld.CreateQuery<EcsPosition, EcsVelocity>(freshFilter);
        MovingQuerySnapshot freshSnapshot = CollectMovingSnapshot(freshQuery);
        kb::tests::Require(freshSnapshot.entityIds == cachedSnapshot.entityIds, "Fresh cached ECS query disagreed with the long-lived query after churn");
    }

    const kb::ecs::WorldTelemetrySnapshot telemetry = cachedWorld.TelemetrySnapshot();
    kb::tests::Require(telemetry.queryCacheHits >= 24U, "ECS query plan cache was not reused during churn stress");
    kb::tests::Require(telemetry.queryCacheMisses >= 1U, "ECS query churn stress did not record the initial query cache miss");
}

void RunTypedEcsQueryStructuralChangeValidationTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 4,
    });

    const kb::ecs::Entity first = world.CreateEntity();
    world.Set(first, EcsPosition{ .x = 1.0F, .y = 0.0F });

    const kb::ecs::Entity second = world.CreateEntity();
    world.Set(second, EcsPosition{ .x = 2.0F, .y = 0.0F });

    const kb::ecs::Entity target = world.CreateEntity();
    world.Set(target, EcsPosition{ .x = 3.0F, .y = 0.0F });

    kb::ecs::Query<EcsPosition> query = world.CreateQuery<EcsPosition>();
    bool queryMutationRejected = false;
    try {
        query.ForEachBatchKernel([&world, target](const kb::ecs::QueryBatch<EcsPosition>& batch) {
            if (batch.Count() != 0) {
                world.Set(target, EcsVelocity{ .x = 1.0F, .y = 0.0F });
            }
        });
    } catch (const std::logic_error&) {
        queryMutationRejected = true;
    }

    kb::tests::Require(queryMutationRejected, "ECS query iteration allowed a direct structural component mutation");
    world.Set(target, EcsVelocity{ .x = 1.0F, .y = 0.0F });
    kb::tests::Require(world.Has<EcsVelocity>(target), "ECS structural iteration guard was not released after query exception");

    bool componentIterationMutationRejected = false;
    try {
        world.ForEach<EcsPosition>(
            [](kb::ecs::Entity entity, const EcsPosition&, void* context) {
                auto* activeWorld = static_cast<kb::ecs::World*>(context);
                activeWorld->DestroyEntity(entity);
            },
            &world);
    } catch (const std::logic_error&) {
        componentIterationMutationRejected = true;
    }

    kb::tests::Require(componentIterationMutationRejected, "ECS component iteration allowed a direct structural entity mutation");
    world.DestroyEntity(second);
    kb::tests::Require(!world.IsAlive(second), "ECS structural iteration guard was not released after component iteration exception");
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
    RunTypedEcsQueryBatchPointerContractTest();
    RunTypedEcsQueryBatchAlignmentContractTest();
    RunTypedEcsQueryBatchWorkStealingTest();
    RunTypedEcsMutableQueryBatchWorkStealingTest();
    RunTypedEcsQueryExecutionPolicyBatchShapeTest();
    RunTypedEcsAdaptiveQueryGrainTest();
    RunTypedEcsParallelQueryScratchReservationTest();
    RunTypedEcsQueryDeterministicReductionModeTest();
    RunTypedEcsQueryPerWorkerReductionScratchTest();
    RunTypedEcsQueryPerWorkerReductionScratchHonorsWorkerOverrideTest();
    RunTypedEcsMutableQueryBatchTest();
    RunTypedEcsMutableQueryBatchMatchesReferenceTest();
#if !defined(NDEBUG)
    RunTypedEcsMutableQueryBorrowLockTest();
    RunTypedEcsParallelMutableQueryBorrowConflictTest();
#endif
    RunTypedEcsQueryBatchKernelTest();
    RunTypedEcsQueryPrefetchHintTest();
    RunTypedEcsQueryIterationOrderPolicyTest();
    RunTypedEcsQueryRepeatedPlanConsistencyTest();
    RunUnsafeHotQueryMutableRangePlanTest();
    RunUnsafeHotQueryAdaptiveRangePlanTest();
    RunTypedEcsQueryStructuralChangeConsistencyTest();
    RunTypedEcsQueryPrepareCacheTracksStorageVersionTest();
    RunTypedEcsQueryCacheChurnStressTest();
    RunTypedEcsQueryStructuralChangeValidationTest();
    RunTypedEcsQueryComponentFilterTest();
    RunTypedEcsQueryChangeFilterTest();
    RunTypedEcsQueryFilterValidationTest();
}

} // namespace kb::tests
