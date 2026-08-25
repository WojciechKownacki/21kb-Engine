#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneRenderProxyComponentMask.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasDeformedGeometry(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.DeformedGeometries().Has(entity);
}
const DrawD3DeformedGeometryComponent* SceneComponentQueryService::TryGetDeformedGeometry(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.DeformedGeometries().TryGet(entity) : nullptr;
}
DrawD3DeformedGeometryComponent* SceneComponentMutationService::TryGetDeformedGeometry(Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.DeformedGeometries().TryGet(entity) : nullptr;
}
bool SceneComponentMutationService::SetDeformedGeometry(Scene& scene, SceneEntity entity, const DrawD3DeformedGeometryComponent& geometry) {
    if (!SceneEntityService::IsAlive(scene, entity) || !IsDrawD3DeformedGeometryComponentPersistable(geometry)) return false;
    if (geometry.poseSource.IsValid() && !SceneEntityService::IsAlive(scene, geometry.poseSource)) return false;
    SceneState& state = SceneAccess::State(scene);
    state.componentStorage.DeformedGeometries().Set(entity, geometry);
    MarkSceneRenderProxyDirty(state, entity);
    MarkScenePrefabNodeDirty(state, entity);
    return true;
}
void SceneComponentMutationService::RemoveDeformedGeometry(Scene& scene, SceneEntity entity) noexcept {
    if (!SceneEntityService::IsAlive(scene, entity)) return;
    SceneState& state = SceneAccess::State(scene);
    state.componentStorage.DeformedGeometries().Remove(entity);
    MarkSceneRenderProxyDirty(state, entity);
    MarkScenePrefabNodeDirty(state, entity);
}
void SceneComponentMutationService::MarkDeformedGeometryModified(Scene& scene, SceneEntity entity) noexcept {
    if (!SceneEntityService::IsAlive(scene, entity)) return;
    SceneState& state = SceneAccess::State(scene);
    state.componentStorage.DeformedGeometries().MarkModified(entity);
    MarkSceneRenderProxyDirty(state, entity);
    MarkScenePrefabNodeDirty(state, entity);
}

} // namespace kb::scene
