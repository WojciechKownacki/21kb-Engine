#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneRenderProxyComponentMask.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasRegionShape(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.RegionShapes().Has(entity);
}

const RegionShapeComponent* SceneComponentQueryService::TryGetRegionShape(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.RegionShapes().TryGet(entity) : nullptr;
}

RegionShapeComponent* SceneComponentMutationService::TryGetRegionShape(Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.RegionShapes().TryGet(entity) : nullptr;
}

void SceneComponentMutationService::SetRegionShape(Scene& scene, SceneEntity entity, const RegionShapeComponent& shape) {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.RegionShapes().Set(entity, shape);
        MarkSceneRenderProxyDirty(state, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

void SceneComponentMutationService::RemoveRegionShape(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.RegionShapes().Remove(entity);
        MarkSceneRenderProxyDirty(state, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

void SceneComponentMutationService::MarkRegionShapeModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.RegionShapes().MarkModified(entity);
        MarkSceneRenderProxyDirty(state, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

} // namespace kb::scene
