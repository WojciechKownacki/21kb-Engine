#include "scene/components/SceneComponentIteration.hpp"

#include "scene/components/SceneMeshRendererIterationDispatcher.hpp"

namespace kb::scene {

void SceneComponentIteration::ForEachMeshRenderer(
    const kb::ecs::World& world,
    std::uint64_t transformComponentId,
    std::uint64_t meshRendererComponentId,
    ecs_query_t*& cachedQuery,
    MeshRendererVisitor visitor,
    void* context) {
    SceneMeshRendererIterationDispatcher::ForEach(world, transformComponentId, 0, meshRendererComponentId, false, cachedQuery, visitor, context);
}

void SceneComponentIteration::ForEachVisibleMeshRenderer(
    const kb::ecs::World& world,
    std::uint64_t transformComponentId,
    std::uint64_t visibilityComponentId,
    std::uint64_t meshRendererComponentId,
    ecs_query_t*& cachedQuery,
    MeshRendererVisitor visitor,
    void* context) {
    SceneMeshRendererIterationDispatcher::ForEach(world, transformComponentId, visibilityComponentId, meshRendererComponentId, true, cachedQuery, visitor, context);
}

} // namespace kb::scene
