#include "EcsTestSuites.hpp"
#include "EcsTestTypes.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/QuerySystem.hpp"
#include "engine/ecs/SystemScheduler.hpp"
#include "engine/ecs/World.hpp"

#include <algorithm>
#include <memory>

namespace {

struct QuerySystemCounters {
    int queryCreated = 0;
    int batches = 0;
    int destroyed = 0;
    std::size_t visited = 0;
    std::size_t maxBatch = 0;
    float lastDelta = 0.0F;
    float sumX = 0.0F;
};

class CountingMovementQuerySystem final : public kb::ecs::QuerySystem<EcsPosition, EcsVelocity> {
public:
    explicit CountingMovementQuerySystem(QuerySystemCounters& counters)
        : QuerySystem(kb::ecs::QueryExecutionSettings{ .maxBatchSize = 64 })
        , counters_(counters) {}

    void OnDestroy(kb::ecs::World& world) override {
        static_cast<void>(world);
        ++counters_.destroyed;
    }

protected:
    void OnQueryCreated(kb::ecs::World& world) override {
        static_cast<void>(world);
        ++counters_.queryCreated;
    }

    void OnUpdateBatch(const Batch& batch, float deltaSeconds) override {
        ++counters_.batches;
        counters_.visited += batch.Count();
        counters_.maxBatch = std::max(counters_.maxBatch, batch.Count());
        counters_.lastDelta = deltaSeconds;

        const EcsPosition* positions = batch.Components<0>();
        const EcsVelocity* velocities = batch.Components<1>();
        for (std::size_t index = 0; index < batch.Count(); ++index) {
            counters_.sumX += positions[index].x + velocities[index].x;
        }
    }

private:
    QuerySystemCounters& counters_;
};

void RunPersistentQuerySystemTest() {
    kb::ecs::World world;
    for (int index = 0; index < 150; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = 2.0F, .y = 0.0F });
        world.Set(entity, EcsVelocity{ .x = 3.0F, .y = 0.0F });
    }

    QuerySystemCounters counters;
    kb::ecs::SystemScheduler scheduler{ kb::ecs::SystemSchedulerConfig{ .profilerEnabled = true } };
    scheduler.Add(std::make_unique<CountingMovementQuerySystem>(counters), world);

    kb::tests::Require(counters.queryCreated == 1, "ECS query system did not create its persistent query");
    scheduler.Update(world, 0.5F);

    kb::tests::Require(counters.visited == 150, "ECS query system did not visit matching entities");
    kb::tests::Require(counters.batches == 3, "ECS query system did not use configured batch execution");
    kb::tests::Require(counters.maxBatch <= 64, "ECS query system exceeded configured batch size");
    kb::tests::Require(kb::tests::NearlyEqual(counters.lastDelta, 0.5F), "ECS query system received invalid delta time");
    kb::tests::Require(kb::tests::NearlyEqual(counters.sumX, 750.0F), "ECS query system saw invalid component data");
    const kb::ecs::SystemSchedulerTrace& trace = scheduler.LastProfilerTrace();
    kb::tests::Require(trace.frameCounters.entitiesProcessed == 150U, "ECS query system did not report processed entities to profiler");
    kb::tests::Require(
        trace.frameCounters.bytesTouched == 150U * (sizeof(EcsPosition) + sizeof(EcsVelocity)),
        "ECS query system did not report touched component bytes to profiler");

    scheduler.Shutdown(world);
    kb::tests::Require(counters.destroyed == 1, "ECS query system was not destroyed by scheduler shutdown");
}

} // namespace

namespace kb::tests {

void RunEcsQuerySystemTests() {
    RunPersistentQuerySystemTest();
}

} // namespace kb::tests
