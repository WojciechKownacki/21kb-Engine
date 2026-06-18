#include "EcsTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/WorkerPool.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

void RunWorkerPoolExecutesConcurrentJobsTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 2 } };

    std::atomic<std::size_t> entered = 0;
    std::atomic<bool> released = false;
    std::atomic<std::size_t> completions = 0;
    struct ConcurrentJobContext {
        std::atomic<std::size_t>* entered = nullptr;
        std::atomic<bool>* released = nullptr;
        std::atomic<std::size_t>* completions = nullptr;
        bool releaseBatch = false;
    };
    ConcurrentJobContext firstJobContext{ &entered, &released, &completions, true };
    ConcurrentJobContext secondJobContext{ &entered, &released, &completions, false };

    std::vector<kb::ecs::WorkerPoolJob> jobs;
    jobs.push_back(kb::ecs::WorkerPoolJob{ .callback = +[](kb::ecs::WorkerContext context, void* userContext) {
        auto& jobContext = *static_cast<ConcurrentJobContext*>(userContext);
        kb::tests::Require(context.workerCount == 2U, "ECS worker pool reported an invalid worker count");
        jobContext.entered->fetch_add(1U, std::memory_order_acq_rel);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 2 };
        if (jobContext.releaseBatch) {
            while (jobContext.entered->load(std::memory_order_acquire) < 2U && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
            kb::tests::Require(jobContext.entered->load(std::memory_order_acquire) == 2U, "ECS worker pool did not execute jobs concurrently");
            jobContext.released->store(true, std::memory_order_release);
        } else {
            while (!jobContext.released->load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
            kb::tests::Require(jobContext.released->load(std::memory_order_acquire), "ECS worker pool did not release the concurrent job batch");
        }
        jobContext.completions->fetch_add(1U, std::memory_order_acq_rel);
    }, .context = &firstJobContext });
    jobs.push_back(kb::ecs::WorkerPoolJob{ .callback = jobs.front().callback, .context = &secondJobContext });

    pool.Run(jobs);

    kb::tests::Require(pool.WorkerCount() == 2U, "ECS worker pool did not keep the configured worker count");
    kb::tests::Require(completions.load(std::memory_order_acquire) == 2U, "ECS worker pool did not finish all jobs");
}

void RunWorkerPoolPropagatesJobExceptionTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 1 } };
    std::vector<kb::ecs::WorkerPoolJob> jobs;
    jobs.push_back(kb::ecs::WorkerPoolJob{ .callback = +[](kb::ecs::WorkerContext, void*) {
        throw std::invalid_argument("worker failure");
    } });

    bool propagated = false;
    try {
        pool.Run(jobs);
    } catch (const std::invalid_argument&) {
        propagated = true;
    }

    kb::tests::Require(propagated, "ECS worker pool did not propagate job exceptions");
}

void RunWorkerPoolCancelsPendingJobsAfterExceptionTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 1 } };
    std::atomic<bool> pendingJobRan = false;

    std::vector<kb::ecs::WorkerPoolJob> jobs;
    jobs.push_back(kb::ecs::WorkerPoolJob{ .callback = +[](kb::ecs::WorkerContext, void*) {
        throw std::invalid_argument("worker failure");
    } });
    jobs.push_back(kb::ecs::WorkerPoolJob{ .callback = +[](kb::ecs::WorkerContext, void* userContext) {
        static_cast<std::atomic<bool>*>(userContext)->store(true, std::memory_order_release);
    }, .context = &pendingJobRan });

    bool propagated = false;
    try {
        pool.Run(jobs);
    } catch (const std::invalid_argument&) {
        propagated = true;
    }

    kb::tests::Require(propagated, "ECS worker pool did not propagate the first job exception");
    kb::tests::Require(!pendingJobRan.load(std::memory_order_acquire), "ECS worker pool executed pending jobs after the first exception");
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

void RunWorkerPoolBatchesHonorWorkerCountLimitTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 4 } };

    std::vector<kb::ecs::WorkerPoolBatch> batches;
    for (std::size_t index = 0; index < 16; ++index) {
        batches.push_back(kb::ecs::WorkerPoolBatch{
            .index = index,
            .begin = index,
            .count = 1,
            .preferredWorkerIndex = index,
            .workerCountLimit = 2,
        });
    }

    std::atomic<std::size_t> visited = 0;
    std::atomic<std::uint32_t> workerMask = 0;

    pool.RunBatches(batches, [&visited, &workerMask](kb::ecs::WorkerContext context, const kb::ecs::WorkerPoolBatch&) {
        kb::tests::Require(context.workerCount == 2U, "ECS worker pool batch dispatch ignored worker count limit");
        kb::tests::Require(context.workerIndex < 2U, "ECS worker pool batch dispatch used a worker outside the dispatch limit");
        workerMask.fetch_or(1U << context.workerIndex, std::memory_order_acq_rel);
        visited.fetch_add(1U, std::memory_order_acq_rel);
    });

    kb::tests::Require(visited.load(std::memory_order_acquire) == batches.size(), "ECS worker pool limited batch dispatch did not visit every batch");
    kb::tests::Require((workerMask.load(std::memory_order_acquire) & ~0b11U) == 0U, "ECS worker pool limited batch dispatch used a forbidden worker slot");
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

void RunWorkerPoolChunksHonorWorkerCountLimitTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 4 } };

    std::vector<kb::ecs::WorkerPoolChunk> chunks;
    for (std::size_t index = 0; index < 16; ++index) {
        chunks.push_back(kb::ecs::WorkerPoolChunk{
            .index = index,
            .begin = index,
            .count = 1,
            .preferredWorkerIndex = index,
            .workerCountLimit = 2,
        });
    }

    std::atomic<std::size_t> visited = 0;
    std::atomic<std::uint32_t> workerMask = 0;

    pool.ParallelForChunks(chunks, [&visited, &workerMask](kb::ecs::WorkerContext context, const kb::ecs::WorkerPoolChunk&) {
        kb::tests::Require(context.workerCount == 2U, "ECS worker pool chunk dispatch ignored worker count limit");
        kb::tests::Require(context.workerIndex < 2U, "ECS worker pool chunk dispatch used a worker outside the dispatch limit");
        workerMask.fetch_or(1U << context.workerIndex, std::memory_order_acq_rel);
        visited.fetch_add(1U, std::memory_order_acq_rel);
    });

    kb::tests::Require(visited.load(std::memory_order_acquire) == chunks.size(), "ECS worker pool limited chunk dispatch did not visit every chunk");
    kb::tests::Require((workerMask.load(std::memory_order_acquire) & ~0b11U) == 0U, "ECS worker pool limited chunk dispatch used a forbidden worker slot");
}

void RunWorkerPoolSubmittedWorkHonorsWorkerCountLimitTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 4 } };

    std::vector<kb::ecs::WorkerPoolBatch> batches;
    std::vector<kb::ecs::WorkerPoolChunk> chunks;
    for (std::size_t index = 0; index < 16; ++index) {
        batches.push_back(kb::ecs::WorkerPoolBatch{
            .index = index,
            .begin = index,
            .count = 1,
            .preferredWorkerIndex = index,
            .workerCountLimit = 2,
        });
        chunks.push_back(kb::ecs::WorkerPoolChunk{
            .index = index,
            .begin = index,
            .count = 1,
            .preferredWorkerIndex = index,
            .workerCountLimit = 2,
        });
    }

    std::atomic<std::size_t> visitedBatches = 0;
    std::atomic<std::size_t> visitedChunks = 0;
    std::atomic<std::uint32_t> workerMask = 0;

    std::pair<std::atomic<std::size_t>*, std::atomic<std::uint32_t>*> batchContext{ &visitedBatches, &workerMask };
    kb::ecs::JobHandle batchHandle = pool.SubmitBatches(std::move(batches), +[](kb::ecs::WorkerContext context, const kb::ecs::WorkerPoolBatch&, void* userContext) {
        auto& counters = *static_cast<std::pair<std::atomic<std::size_t>*, std::atomic<std::uint32_t>*>*>(userContext);
        kb::tests::Require(context.workerCount == 2U, "ECS submitted batch dispatch ignored worker count limit");
        kb::tests::Require(context.workerIndex < 2U, "ECS submitted batch dispatch used a worker outside the dispatch limit");
        counters.second->fetch_or(1U << context.workerIndex, std::memory_order_acq_rel);
        counters.first->fetch_add(1U, std::memory_order_acq_rel);
    }, &batchContext);
    batchHandle.Wait();

    std::pair<std::atomic<std::size_t>*, std::atomic<std::uint32_t>*> chunkContext{ &visitedChunks, &workerMask };
    kb::ecs::JobHandle chunkHandle = pool.SubmitParallelForChunks(std::move(chunks), +[](kb::ecs::WorkerContext context, const kb::ecs::WorkerPoolChunk&, void* userContext) {
        auto& counters = *static_cast<std::pair<std::atomic<std::size_t>*, std::atomic<std::uint32_t>*>*>(userContext);
        kb::tests::Require(context.workerCount == 2U, "ECS submitted chunk dispatch ignored worker count limit");
        kb::tests::Require(context.workerIndex < 2U, "ECS submitted chunk dispatch used a worker outside the dispatch limit");
        counters.second->fetch_or(1U << context.workerIndex, std::memory_order_acq_rel);
        counters.first->fetch_add(1U, std::memory_order_acq_rel);
    }, &chunkContext);
    chunkHandle.Wait();

    kb::tests::Require(visitedBatches.load(std::memory_order_acquire) == 16U, "ECS submitted limited batch dispatch did not visit every batch");
    kb::tests::Require(visitedChunks.load(std::memory_order_acquire) == 16U, "ECS submitted limited chunk dispatch did not visit every chunk");
    kb::tests::Require((workerMask.load(std::memory_order_acquire) & ~0b11U) == 0U, "ECS submitted limited dispatch used a forbidden worker slot");
}

void RunWorkerPoolSynchronousHotPathAcceptsMoveOnlyJobsTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 2 } };

    std::vector<kb::ecs::WorkerPoolBatch> batches{
        kb::ecs::WorkerPoolBatch{ .index = 0, .begin = 0, .count = 3, .preferredWorkerIndex = 0 },
        kb::ecs::WorkerPoolBatch{ .index = 1, .begin = 3, .count = 5, .preferredWorkerIndex = 1 },
    };

    std::atomic<std::size_t> batchRows = 0;
    pool.RunBatches(
        batches,
        [token = std::make_unique<int>(1), &batchRows](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolBatch& batch) mutable {
            kb::tests::Require(token != nullptr, "ECS worker pool lost move-only batch job state");
            batchRows.fetch_add(batch.count, std::memory_order_acq_rel);
        });

    std::atomic<std::size_t> chunkRows = 0;
    pool.ParallelForChunks(
        11,
        4,
        [token = std::make_unique<int>(2), &chunkRows](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) mutable {
            kb::tests::Require(token != nullptr, "ECS worker pool lost move-only chunk job state");
            chunkRows.fetch_add(chunk.count, std::memory_order_acq_rel);
        });

    kb::tests::Require(batchRows.load(std::memory_order_acquire) == 8U, "ECS worker pool move-only batch job did not process every row");
    kb::tests::Require(chunkRows.load(std::memory_order_acquire) == 11U, "ECS worker pool move-only chunk job did not process every row");
}

void RunWorkerPoolJobHandleWaitsForSubmittedJobsTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 1 } };
    std::atomic<std::size_t> completed = 0;

    std::vector<kb::ecs::WorkerPoolJob> jobs;
    jobs.push_back(kb::ecs::WorkerPoolJob{ .callback = +[](kb::ecs::WorkerContext, void* userContext) {
        static_cast<std::atomic<std::size_t>*>(userContext)->fetch_add(1U, std::memory_order_acq_rel);
    }, .context = &completed });

    kb::ecs::JobHandle handle = pool.Submit(std::move(jobs));

    kb::tests::Require(handle.Valid(), "ECS worker pool did not return a valid job handle for submitted work");
    handle.Wait();
    kb::tests::Require(handle.IsReady(), "ECS job handle did not report completion after Wait");
    kb::tests::Require(completed.load(std::memory_order_acquire) == 1U, "ECS job handle returned before submitted work completed");
}

void RunWorkerPoolJobHandlePropagatesSubmittedExceptionTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 1 } };

    std::vector<kb::ecs::WorkerPoolJob> jobs;
    jobs.push_back(kb::ecs::WorkerPoolJob{ .callback = +[](kb::ecs::WorkerContext, void*) {
        throw std::invalid_argument("submitted failure");
    } });

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
    fence.Add(pool.SubmitParallelForChunks(19, 4, +[](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk, void* userContext) {
        static_cast<std::atomic<std::size_t>*>(userContext)->fetch_add(chunk.count, std::memory_order_acq_rel);
    }, &visited));

    kb::tests::Require(!fence.Empty(), "ECS job fence did not retain submitted work");
    kb::tests::Require(fence.Count() == 1U, "ECS job fence reported an invalid handle count");
    fence.Wait();
    kb::tests::Require(fence.IsReady(), "ECS job fence did not report completion after Wait");
    kb::tests::Require(visited.load(std::memory_order_acquire) == 19U, "ECS job fence returned before chunk work completed");
}

void RunWorkerPoolJobFenceWaitsForAllHandlesBeforeRethrowTest() {
    kb::ecs::WorkerPool failingPool{ kb::ecs::WorkerPoolConfig{ .workerCount = 1 } };
    kb::ecs::WorkerPool slowPool{ kb::ecs::WorkerPoolConfig{ .workerCount = 1 } };
    std::atomic<bool> slowJobCompleted = false;

    std::vector<kb::ecs::WorkerPoolJob> failingJobs;
    failingJobs.push_back(kb::ecs::WorkerPoolJob{ .callback = +[](kb::ecs::WorkerContext, void*) {
        throw std::invalid_argument("fence failure");
    } });

    std::vector<kb::ecs::WorkerPoolJob> slowJobs;
    slowJobs.push_back(kb::ecs::WorkerPoolJob{ .callback = +[](kb::ecs::WorkerContext, void* userContext) {
        std::this_thread::sleep_for(std::chrono::milliseconds{ 50 });
        static_cast<std::atomic<bool>*>(userContext)->store(true, std::memory_order_release);
    }, .context = &slowJobCompleted });

    kb::ecs::JobFence fence;
    fence.Add(failingPool.Submit(std::move(failingJobs)));
    fence.Add(slowPool.Submit(std::move(slowJobs)));

    bool propagated = false;
    try {
        fence.Wait();
    } catch (const std::invalid_argument&) {
        propagated = true;
    }

    kb::tests::Require(propagated, "ECS job fence did not propagate the first failed handle");
    kb::tests::Require(slowJobCompleted.load(std::memory_order_acquire), "ECS job fence rethrew before waiting for every handle");
    kb::tests::Require(fence.IsReady(), "ECS failed job fence did not report ready after every handle completed");
}

void RunWorkerPoolCancelsPendingChunksAfterExceptionTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 1 } };
    std::atomic<std::size_t> visited = 0;

    std::vector<kb::ecs::WorkerPoolChunk> chunks{
        kb::ecs::WorkerPoolChunk{ .index = 0, .begin = 0, .count = 1, .preferredWorkerIndex = 0 },
        kb::ecs::WorkerPoolChunk{ .index = 1, .begin = 1, .count = 1, .preferredWorkerIndex = 0 },
    };

    kb::ecs::JobHandle handle = pool.SubmitParallelForChunks(std::move(chunks), +[](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk, void* userContext) {
        auto& counter = *static_cast<std::atomic<std::size_t>*>(userContext);
        if (chunk.index == 0U) {
            throw std::invalid_argument("chunk failure");
        }
        counter.fetch_add(1U, std::memory_order_acq_rel);
    }, &visited);

    bool propagated = false;
    try {
        handle.Wait();
    } catch (const std::invalid_argument&) {
        propagated = true;
    }

    kb::tests::Require(propagated, "ECS worker pool did not propagate the first chunk exception");
    kb::tests::Require(visited.load(std::memory_order_acquire) == 0U, "ECS worker pool executed pending chunks after the first exception");
    kb::tests::Require(handle.IsReady(), "ECS failed chunk handle did not report completion");
}

void RunWorkerPoolSingleThreadModeRunsJobsInlineTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 4, .singleThreaded = true } };

    const std::thread::id callerThread = std::this_thread::get_id();
    std::vector<std::size_t> order;
    struct SingleThreadJobContext {
        const std::thread::id* callerThread = nullptr;
        std::vector<std::size_t>* order = nullptr;
        std::size_t value = 0;
    };
    SingleThreadJobContext firstJobContext{ &callerThread, &order, 1 };
    SingleThreadJobContext secondJobContext{ &callerThread, &order, 2 };
    std::vector<kb::ecs::WorkerPoolJob> jobs;
    jobs.push_back(kb::ecs::WorkerPoolJob{ .callback = +[](kb::ecs::WorkerContext context, void* userContext) {
        auto& jobContext = *static_cast<SingleThreadJobContext*>(userContext);
        kb::tests::Require(std::this_thread::get_id() == *jobContext.callerThread, "ECS single-thread worker pool did not run on the caller thread");
        kb::tests::Require(context.workerIndex == 0U, "ECS single-thread worker pool reported an invalid worker index");
        kb::tests::Require(context.workerCount == 1U, "ECS single-thread worker pool reported an invalid worker count");
        jobContext.order->push_back(jobContext.value);
    }, .context = &firstJobContext });
    jobs.push_back(kb::ecs::WorkerPoolJob{ .callback = jobs.front().callback, .context = &secondJobContext });

    pool.Run(jobs);

    kb::tests::Require(pool.Running(), "ECS single-thread worker pool did not enter the running state");
    kb::tests::Require(pool.WorkerCount() == 1U, "ECS single-thread worker pool did not clamp worker count to one");
    kb::tests::Require(pool.Config().singleThreaded, "ECS worker pool did not preserve the single-thread configuration");
    kb::tests::Require(order == std::vector<std::size_t>{ 1U, 2U }, "ECS single-thread worker pool did not preserve inline job order");
}

void RunWorkerPoolSingleThreadSubmitCompletesHandleTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .singleThreaded = true } };

    std::vector<kb::ecs::WorkerPoolJob> jobs;
    jobs.push_back(kb::ecs::WorkerPoolJob{ .callback = +[](kb::ecs::WorkerContext context, void*) {
        kb::tests::Require(context.workerIndex == 0U && context.workerCount == 1U, "ECS single-thread submitted job received an invalid worker context");
        throw std::invalid_argument("single-thread submitted failure");
    } });

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

void RunWorkerPoolSingleThreadCancelsPendingWorkAfterExceptionTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .singleThreaded = true } };
    std::atomic<bool> pendingJobRan = false;
    std::vector<kb::ecs::WorkerPoolJob> jobs;
    jobs.push_back(kb::ecs::WorkerPoolJob{ .callback = +[](kb::ecs::WorkerContext, void*) {
        throw std::invalid_argument("single-thread failure");
    } });
    jobs.push_back(kb::ecs::WorkerPoolJob{ .callback = +[](kb::ecs::WorkerContext, void* userContext) {
        static_cast<std::atomic<bool>*>(userContext)->store(true, std::memory_order_release);
    }, .context = &pendingJobRan });

    bool propagated = false;
    try {
        pool.Run(jobs);
    } catch (const std::invalid_argument&) {
        propagated = true;
    }

    kb::tests::Require(propagated, "ECS single-thread worker pool did not propagate the first exception");
    kb::tests::Require(!pendingJobRan.load(std::memory_order_acquire), "ECS single-thread worker pool executed pending work after the first exception");
}

void RunWorkerPoolStopCancelsSubmittedHandleTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 1 } };

    std::atomic<bool> firstJobEntered = false;
    std::atomic<bool> stopStarted = false;
    std::atomic<bool> secondJobRan = false;
    struct CancellationJobContext {
        std::atomic<bool>* firstJobEntered = nullptr;
        std::atomic<bool>* stopStarted = nullptr;
        std::atomic<bool>* secondJobRan = nullptr;
        bool waitForStop = false;
    };
    CancellationJobContext firstJobContext{ &firstJobEntered, &stopStarted, nullptr, true };
    CancellationJobContext secondJobContext{ nullptr, nullptr, &secondJobRan, false };

    std::vector<kb::ecs::WorkerPoolJob> jobs;
    jobs.push_back(kb::ecs::WorkerPoolJob{ .callback = +[](kb::ecs::WorkerContext, void* userContext) {
        auto& jobContext = *static_cast<CancellationJobContext*>(userContext);
        if (jobContext.waitForStop) {
            jobContext.firstJobEntered->store(true, std::memory_order_release);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 2 };
            while (!jobContext.stopStarted->load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
            kb::tests::Require(jobContext.stopStarted->load(std::memory_order_acquire), "ECS worker pool stop cancellation test timed out before Stop");
            std::this_thread::sleep_for(std::chrono::milliseconds{ 250 });
            return;
        }
        jobContext.secondJobRan->store(true, std::memory_order_release);
    }, .context = &firstJobContext });
    jobs.push_back(kb::ecs::WorkerPoolJob{ .callback = jobs.front().callback, .context = &secondJobContext });

    kb::ecs::JobHandle handle = pool.Submit(std::move(jobs));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 2 };
    while (!firstJobEntered.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    kb::tests::Require(firstJobEntered.load(std::memory_order_acquire), "ECS worker pool did not start the submitted cancellation test job");

    stopStarted.store(true, std::memory_order_release);
    pool.Stop();

    bool cancelled = false;
    try {
        handle.Wait();
    } catch (const std::runtime_error&) {
        cancelled = true;
    }

    kb::tests::Require(cancelled, "ECS worker pool submitted handle returned success after Stop cancelled pending work");
    kb::tests::Require(!secondJobRan.load(std::memory_order_acquire), "ECS worker pool executed pending work after Stop requested cancellation");
    kb::tests::Require(!pool.Running(), "ECS worker pool still reported running after Stop");
}

} // namespace

namespace kb::tests {

void RunEcsWorkerPoolTests() {
    RunWorkerPoolExecutesConcurrentJobsTest();
    RunWorkerPoolPropagatesJobExceptionTest();
    RunWorkerPoolCancelsPendingJobsAfterExceptionTest();
    RunWorkerPoolStealsPreferredBatchesTest();
    RunWorkerPoolBatchesHonorWorkerCountLimitTest();
    RunWorkerPoolParallelForChunksPartitionsRangeTest();
    RunWorkerPoolParallelForChunksPreservesChunkMetadataTest();
    RunWorkerPoolChunksHonorWorkerCountLimitTest();
    RunWorkerPoolSubmittedWorkHonorsWorkerCountLimitTest();
    RunWorkerPoolSynchronousHotPathAcceptsMoveOnlyJobsTest();
    RunWorkerPoolJobHandleWaitsForSubmittedJobsTest();
    RunWorkerPoolJobHandlePropagatesSubmittedExceptionTest();
    RunWorkerPoolJobFenceWaitsForChunkJobsTest();
    RunWorkerPoolJobFenceWaitsForAllHandlesBeforeRethrowTest();
    RunWorkerPoolCancelsPendingChunksAfterExceptionTest();
    RunWorkerPoolSingleThreadModeRunsJobsInlineTest();
    RunWorkerPoolSingleThreadSubmitCompletesHandleTest();
    RunWorkerPoolSingleThreadCancelsPendingWorkAfterExceptionTest();
    RunWorkerPoolStopCancelsSubmittedHandleTest();
}

} // namespace kb::tests
