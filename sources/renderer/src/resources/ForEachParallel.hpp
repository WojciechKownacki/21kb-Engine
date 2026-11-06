#pragma once

#include "engine/ecs/WorkerPool.hpp"

#include <cstddef>

namespace kb::render {

// The pool every ForEachParallel runs on: one for the process, not one per job type.
[[nodiscard]] inline kb::ecs::WorkerPool& SharedParallelPool() {
    static kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{} };
    return pool;
}

// Runs job(index) for every index below `count` on the engine's worker pool and returns when all are done. A job
// writes only the slot of its own index, so the result is the one a loop would give, whatever the order they finish in.
template <typename Job>
void ForEachParallel(std::size_t count, Job&& job) {
    if (count < 2U) {
        for (std::size_t index = 0U; index < count; ++index) job(index);
        return;
    }
    SharedParallelPool().ParallelForChunks(count, 1U, [&job](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
        for (std::size_t index = chunk.begin; index < chunk.begin + chunk.count; ++index) job(index);
    });
}

} // namespace kb::render
