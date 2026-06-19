#include "scene/SceneAccess.hpp"
#include "scene/SceneIterationService.hpp"
#include "scene/SceneState.hpp"
#include "scene/components/SceneComponentIteration.hpp"

#include <algorithm>
#include <span>

namespace kb::scene {
namespace {

void ForEachUpdatedLightRenderProxyImpl(const Scene& scene, LightRenderProxyVisitor visitor, void* context) {
    if (visitor == nullptr) {
        return;
    }

    const SceneState& state = SceneAccess::State(scene);
    for (const std::size_t proxyIndex : state.transformRenderProxyLightIndices) {
        if (proxyIndex >= state.transformRenderProxyUpdateEntities.size() ||
            proxyIndex >= state.transformRenderProxyWorldAffine3x4.size()) {
            continue;
        }
        const SceneEntity entity = state.transformRenderProxyUpdateEntities[proxyIndex];
        const LightComponent* light = state.componentStorage.Lights().TryGet(entity);
        if (light != nullptr) {
            visitor(entity, state.transformRenderProxyWorldAffine3x4[proxyIndex], *light, context);
        }
    }
}

} // namespace

void SceneIterationService::ForEachLight(const Scene& scene, LightVisitor visitor, void* context) {
    const SceneState& state = SceneAccess::State(scene);
    SceneComponentIteration::ForEachLight(
        state.world,
        state.components.TransformComponentId(),
        state.components.LightComponentId(),
        state.lightIterationQuery,
        visitor,
        context);
}

void SceneIterationService::ForEachUpdatedLightRenderProxy(const Scene& scene, LightRenderProxyVisitor visitor, void* context) {
    ForEachUpdatedLightRenderProxyImpl(scene, visitor, context);
}

} // namespace kb::scene
