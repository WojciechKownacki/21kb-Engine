#include "scene/SceneAccess.hpp"
#include "scene/SceneIterationService.hpp"
#include "scene/SceneState.hpp"
#include "scene/components/SceneComponentIteration.hpp"

#include <algorithm>
#include <span>

namespace kb::scene {
namespace {

void ForEachUpdatedMeshRendererRenderProxyImpl(
    const Scene& scene,
    bool visibleOnly,
    MeshRendererRenderProxyVisitor visitor,
    void* context) {
    if (visitor == nullptr) {
        return;
    }

    const SceneState& state = SceneAccess::State(scene);
    const std::span<const std::size_t> proxyIndices = visibleOnly
        ? std::span<const std::size_t>{ state.transformRenderProxyVisibleMeshRendererIndices }
        : std::span<const std::size_t>{ state.transformRenderProxyMeshRendererIndices };
    for (const std::size_t proxyIndex : proxyIndices) {
        if (proxyIndex >= state.transformRenderProxyUpdateEntities.size() ||
            proxyIndex >= state.transformRenderProxyWorldAffine3x4.size()) {
            continue;
        }
        const SceneEntity entity = state.transformRenderProxyUpdateEntities[proxyIndex];
        const MeshRendererComponent* renderer = state.componentStorage.MeshRenderers().TryGet(entity);
        if (renderer == nullptr) {
            continue;
        }
        visitor(entity, state.transformRenderProxyWorldAffine3x4[proxyIndex], *renderer, context);
    }
}

} // namespace

void SceneIterationService::ForEachMeshRenderer(const Scene& scene, MeshRendererVisitor visitor, void* context) {
    const SceneState& state = SceneAccess::State(scene);
    SceneComponentIteration::ForEachMeshRenderer(
        state.world,
        state.components.TransformComponentId(),
        state.components.MeshRendererComponentId(),
        state.meshRendererIterationQuery,
        visitor,
        context);
}

void SceneIterationService::ForEachVisibleMeshRenderer(const Scene& scene, MeshRendererVisitor visitor, void* context) {
    const SceneState& state = SceneAccess::State(scene);
    SceneComponentIteration::ForEachVisibleMeshRenderer(
        state.world,
        state.components.TransformComponentId(),
        state.components.VisibilityComponentId(),
        state.components.MeshRendererComponentId(),
        state.visibleMeshRendererIterationQuery,
        visitor,
        context);
}

void SceneIterationService::ForEachUpdatedMeshRendererRenderProxy(const Scene& scene, MeshRendererRenderProxyVisitor visitor, void* context) {
    ForEachUpdatedMeshRendererRenderProxyImpl(scene, false, visitor, context);
}

void SceneIterationService::ForEachVisibleUpdatedMeshRendererRenderProxy(const Scene& scene, MeshRendererRenderProxyVisitor visitor, void* context) {
    ForEachUpdatedMeshRendererRenderProxyImpl(scene, true, visitor, context);
}

} // namespace kb::scene
