#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneRenderProxyComponentMask.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

#include <algorithm>

namespace kb::scene {
namespace {

void MarkMeshRendererRenderProxyDirty(SceneState& state, SceneEntity entity) noexcept {
    if (std::ranges::find(state.meshRendererRenderProxyUpdateEntities, entity) == state.meshRendererRenderProxyUpdateEntities.end()) {
        try {
            state.meshRendererRenderProxyUpdateEntities.push_back(entity);
        } catch (...) {
        }
    }
}

} // namespace

bool SceneComponentQueryService::HasMeshRenderer(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.MeshRenderers().Has(entity);
}

const MeshRendererComponent* SceneComponentQueryService::TryGetMeshRenderer(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.MeshRenderers().TryGet(entity) : nullptr;
}

MeshRendererComponent* SceneComponentMutationService::TryGetMeshRenderer(Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.MeshRenderers().TryGet(entity) : nullptr;
}

void SceneComponentMutationService::SetMeshRenderer(Scene& scene, SceneEntity entity, const MeshRendererComponent& renderer) {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.MeshRenderers().Set(entity, renderer);
        SetSceneRenderProxyComponentMask(state, entity, SceneRenderProxyComponentMask::MeshRenderer);
        MarkMeshRendererRenderProxyDirty(state, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

void SceneComponentMutationService::RemoveMeshRenderer(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.MeshRenderers().Remove(entity);
        ClearSceneRenderProxyComponentMask(state, entity, SceneRenderProxyComponentMask::MeshRenderer);
        MarkMeshRendererRenderProxyDirty(state, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

void SceneComponentMutationService::MarkMeshRendererModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.MeshRenderers().MarkModified(entity);
        MarkMeshRendererRenderProxyDirty(state, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

} // namespace kb::scene
