#include "scene/SceneState.hpp"
#include "scene/SceneStreamingService.hpp"
#include "scene/components/SceneComponentIteration.hpp"
#include "scene/transform/SceneTransformRootQueryCache.hpp"
#include "world/WorldPartitionState.hpp"

#include <flecs.h>

namespace kb::scene {

SceneState::SceneState()
    : components(world)
    , componentStorage(world, components) {}

SceneState::SceneState(kb::ecs::WorldConfig worldConfig)
    : world(worldConfig)
    , components(world)
    , componentStorage(world, components) {}

SceneState::~SceneState() {
    // Join the in-flight animator debug snapshot build before any member it
    // reads (animator records, pose buffers, the publisher) is destroyed.
    // The job callback never throws, so this wait cannot rethrow here.
    animatorDebugSnapshotJob.Wait();
    if (physicsBodyIterationQuery != nullptr) {
        ecs_query_fini(physicsBodyIterationQuery);
        physicsBodyIterationQuery = nullptr;
    }
    // The open history command is destroyed before the instance registry that reports to it.
    prefabInstances.SetJournal(nullptr);
}

SceneComponentIterationQueries& SceneState::ComponentIterationQueries() const {
    if (componentIterationQueries == nullptr) {
        componentIterationQueries = std::make_unique<SceneComponentIterationQueries>();
    }
    return *componentIterationQueries;
}

} // namespace kb::scene
