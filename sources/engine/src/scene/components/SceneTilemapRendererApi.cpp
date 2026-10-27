#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneHistoryService.hpp"
#include "scene/SceneRenderProxyComponentMask.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"
#include <engine/scene/Scene.hpp>
#include <engine/scene/SceneHierarchyAccess.hpp>

namespace kb::scene {
namespace {
void MarkLayerDirty(Scene& scene, SceneEntity entity) {
    MarkSceneRenderProxyDirty(SceneAccess::State(scene), entity);
    for (const auto child : scene.Hierarchy().ChildEntities(entity)) MarkLayerDirty(scene, child);
}
}

bool SceneComponentQueryService::HasTilemapRenderer(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) &&
           SceneAccess::State(scene).componentStorage.TilemapRenderers().Has(entity);
}
const TilemapRendererComponent* SceneComponentQueryService::TryGetTilemapRenderer(const Scene& scene,
                                                                                  SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity)
               ? SceneAccess::State(scene).componentStorage.TilemapRenderers().TryGet(entity)
               : nullptr;
}
TilemapRendererComponent* SceneComponentMutationService::TryGetTilemapRenderer(Scene& scene,
                                                                               SceneEntity entity) noexcept {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    return SceneEntityService::IsAlive(scene, entity)
               ? SceneAccess::State(scene).componentStorage.TilemapRenderers().TryGet(entity)
               : nullptr;
}
void SceneComponentMutationService::SetTilemapRenderer(Scene& scene, SceneEntity entity,
                                                       const TilemapRendererComponent& component) {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.TilemapRenderers().Set(entity, component);
        MarkLayerDirty(scene, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}
void SceneComponentMutationService::RemoveTilemapRenderer(Scene& scene, SceneEntity entity) noexcept {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.TilemapRenderers().Remove(entity);
        MarkLayerDirty(scene, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}
void SceneComponentMutationService::MarkTilemapRendererModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.TilemapRenderers().MarkModified(entity);
        MarkLayerDirty(scene, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

} // namespace kb::scene
