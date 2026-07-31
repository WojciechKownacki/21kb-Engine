#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

#include "engine/scene/SceneTagCatalog.hpp"
#include "engine/scene/Scene.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasTags(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.Tags().Has(entity);
}

const TagsComponent* SceneComponentQueryService::TryGetTags(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Tags().TryGet(entity) : nullptr;
}

TagsComponent* SceneComponentMutationService::TryGetTags(Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Tags().TryGet(entity) : nullptr;
}

void SceneComponentMutationService::SetTags(Scene& scene, SceneEntity entity, const TagsComponent& tags) {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Tags().Set(entity, tags);
        scene.Tags().RegisterAssignedTags(entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

void SceneComponentMutationService::RemoveTags(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Tags().Remove(entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

void SceneComponentMutationService::MarkTagsModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Tags().MarkModified(entity);
        scene.Tags().RegisterAssignedTags(entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

} // namespace kb::scene
