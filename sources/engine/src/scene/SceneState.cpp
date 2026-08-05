#include "scene/SceneState.hpp"

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
    if (cameraIterationQuery != nullptr) {
        ecs_query_fini(cameraIterationQuery);
        cameraIterationQuery = nullptr;
    }
    if (lightIterationQuery != nullptr) {
        ecs_query_fini(lightIterationQuery);
        lightIterationQuery = nullptr;
    }
    if (meshRendererIterationQuery != nullptr) {
        ecs_query_fini(meshRendererIterationQuery);
        meshRendererIterationQuery = nullptr;
    }
    if (visibleMeshRendererIterationQuery != nullptr) {
        ecs_query_fini(visibleMeshRendererIterationQuery);
        visibleMeshRendererIterationQuery = nullptr;
    }
    if (physicsBodyIterationQuery != nullptr) {
        ecs_query_fini(physicsBodyIterationQuery);
        physicsBodyIterationQuery = nullptr;
    }
}

} // namespace kb::scene
