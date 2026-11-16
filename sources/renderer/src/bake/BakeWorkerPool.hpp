#pragma once

#include "engine/ecs/WorkerPool.hpp"

namespace kb::render::bake {

// The one worker pool of the host-side bakers, started on first use, so a cook that bakes meshes
// and textures never keeps two sets of threads.
[[nodiscard]] inline kb::ecs::WorkerPool& BakeWorkerPool() {
    static kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{} };
    return pool;
}

} // namespace kb::render::bake
