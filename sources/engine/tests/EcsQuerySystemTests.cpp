#include "EcsTestSuites.hpp"
#include "EcsTestTypes.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/QuerySystem.hpp"
#include "engine/ecs/SystemScheduler.hpp"
#include "engine/ecs/World.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

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

class ParallelProbeQuerySystem final : public kb::ecs::QuerySystem<EcsPosition, EcsVelocity> {
public:
    ParallelProbeQuerySystem(std::atomic<int>& running, std::atomic<int>& maxRunning, std::atomic<int>& batches)
        : QuerySystem(kb::ecs::QueryExecutionSettings{ .maxBatchSize = 16 })
        , running_(running)
        , maxRunning_(maxRunning)
        , batches_(batches) {}

protected:
    void OnUpdateBatch(const Batch& batch, float deltaSeconds) override {
        static_cast<void>(batch);
        static_cast<void>(deltaSeconds);
        batches_.fetch_add(1, std::memory_order_acq_rel);
        const int active = running_.fetch_add(1, std::memory_order_acq_rel) + 1;
        int observed = maxRunning_.load(std::memory_order_acquire);
        while (active > observed && !maxRunning_.compare_exchange_weak(observed, active, std::memory_order_acq_rel)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds{ 5 });
        running_.fetch_sub(1, std::memory_order_acq_rel);
    }

private:
    std::atomic<int>& running_;
    std::atomic<int>& maxRunning_;
    std::atomic<int>& batches_;
};

class DeterministicProbeQuerySystem final : public kb::ecs::QuerySystem<EcsPosition, EcsVelocity> {
public:
    DeterministicProbeQuerySystem(std::atomic<int>& running, std::atomic<int>& maxRunning, std::vector<float>& visited)
        : QuerySystem(kb::ecs::QueryExecutionSettings{
              .maxBatchSize = 7,
              .iterationOrder = kb::ecs::QueryIterationOrder::Deterministic,
          })
        , running_(running)
        , maxRunning_(maxRunning)
        , visited_(visited) {}

protected:
    void OnUpdateBatch(const Batch& batch, float deltaSeconds) override {
        static_cast<void>(deltaSeconds);
        const int active = running_.fetch_add(1, std::memory_order_acq_rel) + 1;
        int observed = maxRunning_.load(std::memory_order_acquire);
        while (active > observed && !maxRunning_.compare_exchange_weak(observed, active, std::memory_order_acq_rel)) {}

        const EcsPosition* positions = batch.Components<0>();
        for (std::size_t index = 0; index < batch.Count(); ++index) {
            visited_.push_back(positions[index].x);
        }

        running_.fetch_sub(1, std::memory_order_acq_rel);
    }

private:
    std::atomic<int>& running_;
    std::atomic<int>& maxRunning_;
    std::vector<float>& visited_;
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
    kb::tests::Require(trace.frameCounters.chunkJobsCount == 3U, "ECS query system did not report query batch jobs to profiler");
    kb::tests::Require(
        trace.frameCounters.bytesTouched == 150U * (sizeof(EcsPosition) + sizeof(EcsVelocity)),
        "ECS query system did not report touched component bytes to profiler");
    kb::tests::Require(trace.events.size() == 1U && trace.events[0].executionPath == "query_callback", "ECS query system trace did not report query callback execution path");
    kb::tests::Require(trace.events[0].chunkJobsCount == 3U, "ECS query system trace did not report query batch jobs");
    kb::tests::Require(trace.stageCounters.size() == 1U && trace.stageCounters[0].chunkJobsCount == 3U, "ECS query system stage counters did not report query batch jobs");
    kb::tests::Require(trace.systemCounters.size() == 1U && trace.systemCounters[0].executionPath == "query_callback", "ECS query system counters did not report query callback execution path");
    kb::tests::Require(trace.systemCounters[0].chunkJobsCount == 3U, "ECS query system counters did not report query batch jobs");

    scheduler.Shutdown(world);
    kb::tests::Require(counters.destroyed == 1, "ECS query system was not destroyed by scheduler shutdown");
}

void RunSchedulerQueryChunkParallelismModeTest() {
    kb::ecs::World world;
    for (int index = 0; index < 256; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = 1.0F, .y = 0.0F });
        world.Set(entity, EcsVelocity{ .x = 1.0F, .y = 0.0F });
    }

    std::atomic<int> running{ 0 };
    std::atomic<int> maxRunning{ 0 };
    std::atomic<int> batches{ 0 };

    kb::ecs::WorkerPoolConfig workerConfig;
    workerConfig.workerCount = 2;
    kb::ecs::SystemScheduler scheduler{ kb::ecs::SystemSchedulerConfig{
        .profilerEnabled = true,
        .parallelExecutionEnabled = true,
        .parallelismMode = kb::ecs::SystemSchedulerParallelismMode::QueryChunks,
        .workerPool = workerConfig,
    } };
    scheduler.Add(std::make_unique<ParallelProbeQuerySystem>(running, maxRunning, batches), world);

    scheduler.Update(world, 0.0F);

    kb::tests::Require(batches.load(std::memory_order_acquire) > 1, "ECS query chunk parallelism test did not split work into batches");
    kb::tests::Require(maxRunning.load(std::memory_order_acquire) > 1, "ECS scheduler did not provide its worker pool to query systems");
    kb::tests::Require(scheduler.LastProfilerTrace().frameCounters.parallelStageCount == 0U, "ECS query chunk mode should not also run system stages in parallel");

    scheduler.Shutdown(world);
}

void RunSchedulerDeterministicQueryChunkModeStaysSerialTest() {
    kb::ecs::World world;
    constexpr int kEntityCount = 31;
    for (int index = 0; index < kEntityCount; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 0.0F });
        world.Set(entity, EcsVelocity{ .x = 1.0F, .y = 0.0F });
    }

    std::atomic<int> running{ 0 };
    std::atomic<int> maxRunning{ 0 };
    std::vector<float> firstVisit;
    std::vector<float> secondVisit;
    firstVisit.reserve(kEntityCount);
    secondVisit.reserve(kEntityCount);

    kb::ecs::WorkerPoolConfig workerConfig;
    workerConfig.workerCount = 2;
    kb::ecs::SystemSchedulerConfig config{
        .parallelExecutionEnabled = true,
        .parallelismMode = kb::ecs::SystemSchedulerParallelismMode::QueryChunks,
        .workerPool = workerConfig,
    };

    {
        kb::ecs::SystemScheduler scheduler{ config };
        scheduler.Add(std::make_unique<DeterministicProbeQuerySystem>(running, maxRunning, firstVisit), world);
        scheduler.Update(world, 0.0F);
        scheduler.Shutdown(world);
    }

    kb::tests::Require(maxRunning.load(std::memory_order_acquire) == 1, "ECS deterministic query iteration used parallel chunk workers");

    running.store(0, std::memory_order_release);
    maxRunning.store(0, std::memory_order_release);

    {
        kb::ecs::SystemScheduler scheduler{ config };
        scheduler.Add(std::make_unique<DeterministicProbeQuerySystem>(running, maxRunning, secondVisit), world);
        scheduler.Update(world, 0.0F);
        scheduler.Shutdown(world);
    }

    kb::tests::Require(maxRunning.load(std::memory_order_acquire) == 1, "ECS deterministic query iteration was not stable on repeat execution");
    kb::tests::Require(firstVisit.size() == static_cast<std::size_t>(kEntityCount), "ECS deterministic query iteration missed entities");
    kb::tests::Require(firstVisit == secondVisit, "ECS deterministic query iteration produced different batch order across runs");
    for (int index = 0; index < kEntityCount; ++index) {
        kb::tests::Require(firstVisit[static_cast<std::size_t>(index)] == static_cast<float>(index), "ECS deterministic query iteration did not preserve stable entity order");
    }
}

} // namespace

namespace kb::tests {

void RunEcsQuerySystemTests() {
    RunPersistentQuerySystemTest();
    RunSchedulerQueryChunkParallelismModeTest();
    RunSchedulerDeterministicQueryChunkModeStaysSerialTest();
}

} // namespace kb::tests
