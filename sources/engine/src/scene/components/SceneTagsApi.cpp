#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"

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
        SceneAccess::State(scene).componentStorage.Tags().Set(entity, tags);
    }
}

void SceneComponentMutationService::RemoveTags(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.Tags().Remove(entity);
    }
}

void SceneComponentMutationService::MarkTagsModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.Tags().MarkModified(entity);
    }
}

} // namespace kb::scene
