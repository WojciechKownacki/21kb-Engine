#pragma once

#include <cstdint>

namespace kb::scene {

class SceneSystemScheduler;

// Counters for the parallel execution of scene systems (see engine/scene/ParallelSceneSystem.hpp).
struct SceneSystemParallelStats {
    std::uint64_t parallelGroups = 0U;      // groups of two or more systems run at the same time
    std::uint64_t parallelSystemRuns = 0U;  // system updates that ran inside such a group
    std::uint64_t serialSystemRuns = 0U;    // updates run one after another while parallel systems were registered
    std::uint64_t conflictBarriers = 0U;    // groups cut short because the next system conflicted
};

// Parallel execution is on for a scheduler by default once a system implements IParallelSceneSystem.
// Turning it off runs every system serially in registration order, which is the reference behaviour
// the tests compare against. It does nothing for a scheduler without parallel systems.
void SetSceneSystemParallelExecution(SceneSystemScheduler& scheduler, bool enabled);
[[nodiscard]] SceneSystemParallelStats GetSceneSystemParallelStats(const SceneSystemScheduler& scheduler);
// Workers the scheduler's own pool has once created; 0 before the first parallel group.
[[nodiscard]] std::uint32_t SceneSystemParallelWorkerCount(const SceneSystemScheduler& scheduler);

} // namespace kb::scene
