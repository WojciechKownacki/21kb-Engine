#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"

namespace kb::scene {

VisibilityComponent SceneComponentQueryService::Visibility(const Scene& scene, SceneEntity entity) {
    const VisibilityComponent* visibility = TryGetVisibility(scene, entity);
    return visibility == nullptr ? VisibilityComponent{} : *visibility;
}

const VisibilityComponent* SceneComponentQueryService::TryGetVisibility(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Visibility().TryGet(entity) : nullptr;
}

VisibilityComponent* SceneComponentMutationService::TryGetVisibility(Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Visibility().TryGet(entity) : nullptr;
}

void SceneComponentMutationService::SetVisibility(Scene& scene, SceneEntity entity, const VisibilityComponent& visibility) {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.Visibility().Set(entity, visibility);
    }
}

void SceneComponentMutationService::MarkVisibilityModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.Visibility().MarkModified(entity);
    }
}

} // namespace kb::scene
