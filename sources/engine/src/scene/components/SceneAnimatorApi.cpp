#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasAnimator(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.Animators().Has(entity);
}
const Animator* SceneComponentQueryService::TryGetAnimator(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Animators().TryGet(entity) : nullptr;
}
Animator* SceneComponentMutationService::TryGetAnimator(Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Animators().TryGet(entity) : nullptr;
}
void SceneComponentMutationService::SetAnimator(Scene& scene, SceneEntity entity, const Animator& animator) {
    if (!SceneEntityService::IsAlive(scene, entity)) return;
    SceneState& state = SceneAccess::State(scene);
    state.componentStorage.Animators().Set(entity, animator);
    MarkScenePrefabNodeDirty(state, entity);
}
void SceneComponentMutationService::RemoveAnimator(Scene& scene, SceneEntity entity) noexcept {
    if (!SceneEntityService::IsAlive(scene, entity)) return;
    SceneState& state = SceneAccess::State(scene);
    state.componentStorage.Animators().Remove(entity);
    state.animators.erase(entity.Id());
    MarkScenePrefabNodeDirty(state, entity);
}
void SceneComponentMutationService::MarkAnimatorModified(Scene& scene, SceneEntity entity) noexcept {
    if (!SceneEntityService::IsAlive(scene, entity)) return;
    SceneState& state = SceneAccess::State(scene);
    state.componentStorage.Animators().MarkModified(entity);
    MarkScenePrefabNodeDirty(state, entity);
}

} // namespace kb::scene
