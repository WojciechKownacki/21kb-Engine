#include "EcsTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/WorkerPool.hpp"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

void RunWorkerPoolExecutesConcurrentJobsTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 2 } };

    std::atomic<std::size_t> entered = 0;
    std::atomic<bool> released = false;
    std::atomic<std::size_t> completions = 0;

    std::vector<kb::ecs::WorkerPoolJob> jobs;
    jobs.emplace_back([&entered, &released, &completions](kb::ecs::WorkerContext context) {
        kb::tests::Require(context.workerCount == 2U, "ECS worker pool reported an invalid worker count");
        entered.fetch_add(1U, std::memory_order_acq_rel);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 2 };
        while (entered.load(std::memory_order_acquire) < 2U && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        kb::tests::Require(entered.load(std::memory_order_acquire) == 2U, "ECS worker pool did not execute jobs concurrently");
        released.store(true, std::memory_order_release);
        completions.fetch_add(1U, std::memory_order_acq_rel);
    });
    jobs.emplace_back([&entered, &released, &completions](kb::ecs::WorkerContext context) {
        kb::tests::Require(context.workerCount == 2U, "ECS worker pool reported an invalid worker count");
        entered.fetch_add(1U, std::memory_order_acq_rel);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 2 };
        while (!released.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        kb::tests::Require(released.load(std::memory_order_acquire), "ECS worker pool did not release the concurrent job batch");
        completions.fetch_add(1U, std::memory_order_acq_rel);
    });

    pool.Run(jobs);

    kb::tests::Require(pool.WorkerCount() == 2U, "ECS worker pool did not keep the configured worker count");
    kb::tests::Require(completions.load(std::memory_order_acquire) == 2U, "ECS worker pool did not finish all jobs");
}

void RunWorkerPoolPropagatesJobExceptionTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 1 } };
    std::vector<kb::ecs::WorkerPoolJob> jobs;
    jobs.emplace_back([](kb::ecs::WorkerContext) {
        throw std::invalid_argument("worker failure");
    });

    bool propagated = false;
    try {
        pool.Run(jobs);
    } catch (const std::invalid_argument&) {
        propagated = true;
    }

    kb::tests::Require(propagated, "ECS worker pool did not propagate job exceptions");
}

void RunWorkerPoolStealsPreferredBatchesTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 2 } };

    std::vector<kb::ecs::WorkerPoolBatch> batches;
    for (std::size_t index = 0; index < 16; ++index) {
        batches.push_back(kb::ecs::WorkerPoolBatch{
            .index = index,
            .begin = index * 4,
            .count = 4,
            .preferredWorkerIndex = 0,
        });
    }

    std::atomic<std::size_t> processed = 0;
    std::atomic<std::size_t> stolen = 0;
    std::atomic<bool> workerZeroBlocked = false;

    pool.RunBatches(batches, [&processed, &stolen, &workerZeroBlocked](kb::ecs::WorkerContext context, const kb::ecs::WorkerPoolBatch& batch) {
        if (context.workerIndex != batch.preferredWorkerIndex) {
            stolen.fetch_add(1U, std::memory_order_acq_rel);
        }

        if (context.workerIndex == 0 && !workerZeroBlocked.exchange(true, std::memory_order_acq_rel)) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 2 };
            while (stolen.load(std::memory_order_acquire) == 0 && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
        }

        kb::tests::Require(batch.count == 4U, "ECS worker pool batch passed an invalid query batch size");
        processed.fetch_add(batch.count, std::memory_order_acq_rel);
    });

    kb::tests::Require(stolen.load(std::memory_order_acquire) > 0, "ECS worker pool did not steal preferred query batches");
    kb::tests::Require(processed.load(std::memory_order_acquire) == 64U, "ECS worker pool did not process every query batch row");
}

void RunWorkerPoolParallelForChunksPartitionsRangeTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 2 } };

    std::vector<std::atomic<std::size_t>> visits(17);
    std::atomic<std::size_t> chunks = 0;

    pool.ParallelForChunks(17, 5, [&visits, &chunks](kb::ecs::WorkerContext context, const kb::ecs::WorkerPoolChunk& chunk) {
        kb::tests::Require(context.workerCount == 2U, "ECS chunk worker reported an invalid worker count");
        kb::tests::Require(chunk.count <= 5U, "ECS parallel-for chunk exceeded the requested chunk size");
        for (std::size_t offset = 0; offset < chunk.count; ++offset) {
            const std::size_t item = chunk.begin + offset;
            kb::tests::Require(item < visits.size(), "ECS parallel-for chunk exceeded the requested range");
            visits[item].fetch_add(1U, std::memory_order_acq_rel);
        }
        chunks.fetch_add(1U, std::memory_order_acq_rel);
    });

    kb::tests::Require(chunks.load(std::memory_order_acquire) == 4U, "ECS parallel-for did not create the expected chunk count");
    for (const std::atomic<std::size_t>& visitCount : visits) {
        kb::tests::Require(visitCount.load(std::memory_order_acquire) == 1U, "ECS parallel-for did not visit each item exactly once");
    }
}

void RunWorkerPoolParallelForChunksPreservesChunkMetadataTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 2 } };
    const std::vector<kb::ecs::WorkerPoolChunk> chunks{
        kb::ecs::WorkerPoolChunk{ .index = 10, .begin = 0, .count = 3, .preferredWorkerIndex = 0 },
        kb::ecs::WorkerPoolChunk{ .index = 20, .begin = 3, .count = 4, .preferredWorkerIndex = 1 },
    };

    std::atomic<std::size_t> indexSum = 0;
    std::atomic<std::size_t> visited = 0;

    pool.ParallelForChunks(chunks, [&indexSum, &visited](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
        indexSum.fetch_add(chunk.index, std::memory_order_acq_rel);
        visited.fetch_add(chunk.count, std::memory_order_acq_rel);
    });

    kb::tests::Require(indexSum.load(std::memory_order_acquire) == 30U, "ECS parallel-for did not preserve caller chunk indexes");
    kb::tests::Require(visited.load(std::memory_order_acquire) == 7U, "ECS parallel-for did not process caller chunk counts");
}

void RunWorkerPoolJobHandleWaitsForSubmittedJobsTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 1 } };
    std::atomic<std::size_t> completed = 0;

    std::vector<kb::ecs::WorkerPoolJob> jobs;
    jobs.emplace_back([&completed](kb::ecs::WorkerContext) {
        completed.fetch_add(1U, std::memory_order_acq_rel);
    });

    kb::ecs::JobHandle handle = pool.Submit(std::move(jobs));

    kb::tests::Require(handle.Valid(), "ECS worker pool did not return a valid job handle for submitted work");
    handle.Wait();
    kb::tests::Require(handle.IsReady(), "ECS job handle did not report completion after Wait");
    kb::tests::Require(completed.load(std::memory_order_acquire) == 1U, "ECS job handle returned before submitted work completed");
}

void RunWorkerPoolJobHandlePropagatesSubmittedExceptionTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 1 } };

    std::vector<kb::ecs::WorkerPoolJob> jobs;
    jobs.emplace_back([](kb::ecs::WorkerContext) {
        throw std::invalid_argument("submitted failure");
    });

    kb::ecs::JobHandle handle = pool.Submit(std::move(jobs));

    bool propagated = false;
    try {
        handle.Wait();
    } catch (const std::invalid_argument&) {
        propagated = true;
    }

    kb::tests::Require(propagated, "ECS job handle did not propagate submitted job exceptions");
    kb::tests::Require(handle.IsReady(), "ECS failed job handle did not report completion");
}

void RunWorkerPoolJobFenceWaitsForChunkJobsTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 2 } };
    std::atomic<std::size_t> visited = 0;

    kb::ecs::JobFence fence;
    fence.Add(pool.SubmitParallelForChunks(19, 4, [&visited](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
        visited.fetch_add(chunk.count, std::memory_order_acq_rel);
    }));

    kb::tests::Require(!fence.Empty(), "ECS job fence did not retain submitted work");
    kb::tests::Require(fence.Count() == 1U, "ECS job fence reported an invalid handle count");
    fence.Wait();
    kb::tests::Require(fence.IsReady(), "ECS job fence did not report completion after Wait");
    kb::tests::Require(visited.load(std::memory_order_acquire) == 19U, "ECS job fence returned before chunk work completed");
}

void RunWorkerPoolSingleThreadModeRunsJobsInlineTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 4, .singleThreaded = true } };

    const std::thread::id callerThread = std::this_thread::get_id();
    std::vector<std::size_t> order;
    std::vector<kb::ecs::WorkerPoolJob> jobs;
    jobs.emplace_back([&callerThread, &order](kb::ecs::WorkerContext context) {
        kb::tests::Require(std::this_thread::get_id() == callerThread, "ECS single-thread worker pool did not run on the caller thread");
        kb::tests::Require(context.workerIndex == 0U, "ECS single-thread worker pool reported an invalid worker index");
        kb::tests::Require(context.workerCount == 1U, "ECS single-thread worker pool reported an invalid worker count");
        order.push_back(1U);
    });
    jobs.emplace_back([&callerThread, &order](kb::ecs::WorkerContext context) {
        kb::tests::Require(std::this_thread::get_id() == callerThread, "ECS single-thread worker pool did not keep execution inline");
        kb::tests::Require(context.workerIndex == 0U && context.workerCount == 1U, "ECS single-thread worker pool changed worker context between jobs");
        order.push_back(2U);
    });

    pool.Run(jobs);

    kb::tests::Require(pool.Running(), "ECS single-thread worker pool did not enter the running state");
    kb::tests::Require(pool.WorkerCount() == 1U, "ECS single-thread worker pool did not clamp worker count to one");
    kb::tests::Require(pool.Config().singleThreaded, "ECS worker pool did not preserve the single-thread configuration");
    kb::tests::Require(order == std::vector<std::size_t>{ 1U, 2U }, "ECS single-thread worker pool did not preserve inline job order");
}

void RunWorkerPoolSingleThreadSubmitCompletesHandleTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .singleThreaded = true } };

    std::vector<kb::ecs::WorkerPoolJob> jobs;
    jobs.emplace_back([](kb::ecs::WorkerContext context) {
        kb::tests::Require(context.workerIndex == 0U && context.workerCount == 1U, "ECS single-thread submitted job received an invalid worker context");
        throw std::invalid_argument("single-thread submitted failure");
    });

    kb::ecs::JobHandle handle = pool.Submit(std::move(jobs));

    kb::tests::Require(handle.Valid(), "ECS single-thread submit did not return a valid job handle");
    kb::tests::Require(handle.IsReady(), "ECS single-thread submit did not complete its job handle inline");

    bool propagated = false;
    try {
        handle.Wait();
    } catch (const std::invalid_argument&) {
        propagated = true;
    }

    kb::tests::Require(propagated, "ECS single-thread job handle did not propagate submitted job exceptions");
}

} // namespace

namespace kb::tests {

void RunEcsWorkerPoolTests() {
    RunWorkerPoolExecutesConcurrentJobsTest();
    RunWorkerPoolPropagatesJobExceptionTest();
    RunWorkerPoolStealsPreferredBatchesTest();
    RunWorkerPoolParallelForChunksPartitionsRangeTest();
    RunWorkerPoolParallelForChunksPreservesChunkMetadataTest();
    RunWorkerPoolJobHandleWaitsForSubmittedJobsTest();
    RunWorkerPoolJobHandlePropagatesSubmittedExceptionTest();
    RunWorkerPoolJobFenceWaitsForChunkJobsTest();
    RunWorkerPoolSingleThreadModeRunsJobsInlineTest();
    RunWorkerPoolSingleThreadSubmitCompletesHandleTest();
}

} // namespace kb::tests
