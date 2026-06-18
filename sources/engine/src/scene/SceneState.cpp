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
}

} // namespace kb::scene
