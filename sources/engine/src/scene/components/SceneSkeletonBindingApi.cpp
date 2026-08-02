#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasSkeletonBinding(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.SkeletonBindings().Has(entity);
}
const SkeletonBindingComponent* SceneComponentQueryService::TryGetSkeletonBinding(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.SkeletonBindings().TryGet(entity) : nullptr;
}
SkeletonBindingComponent* SceneComponentMutationService::TryGetSkeletonBinding(Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.SkeletonBindings().TryGet(entity) : nullptr;
}
bool SceneComponentMutationService::SetSkeletonBinding(Scene& scene, SceneEntity entity, const SkeletonBindingComponent& binding) {
    if (!SceneEntityService::IsAlive(scene, entity) || !IsSkeletonBindingComponentValid(binding)) return false;
    SceneState& state = SceneAccess::State(scene);
    state.componentStorage.SkeletonBindings().Set(entity, binding);
    MarkScenePrefabNodeDirty(state, entity);
    return true;
}
void SceneComponentMutationService::RemoveSkeletonBinding(Scene& scene, SceneEntity entity) noexcept {
    if (!SceneEntityService::IsAlive(scene, entity)) return;
    SceneState& state = SceneAccess::State(scene);
    state.componentStorage.SkeletonBindings().Remove(entity);
    MarkScenePrefabNodeDirty(state, entity);
}
void SceneComponentMutationService::MarkSkeletonBindingModified(Scene& scene, SceneEntity entity) noexcept {
    if (!SceneEntityService::IsAlive(scene, entity)) return;
    SceneState& state = SceneAccess::State(scene);
    state.componentStorage.SkeletonBindings().MarkModified(entity);
    MarkScenePrefabNodeDirty(state, entity);
}

} // namespace kb::scene
