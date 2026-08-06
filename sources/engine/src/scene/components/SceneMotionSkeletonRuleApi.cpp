#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasMotionSkeletonRule(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.MotionSkeletonRules().Has(entity);
}
const MotionSkeletonRuleComponent* SceneComponentQueryService::TryGetMotionSkeletonRule(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.MotionSkeletonRules().TryGet(entity) : nullptr;
}
MotionSkeletonRuleComponent* SceneComponentMutationService::TryGetMotionSkeletonRule(Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.MotionSkeletonRules().TryGet(entity) : nullptr;
}
bool SceneComponentMutationService::SetMotionSkeletonRule(Scene& scene, SceneEntity entity, const MotionSkeletonRuleComponent& rule) {
    if (!SceneEntityService::IsAlive(scene, entity) || !IsMotionSkeletonRuleComponentPersistable(rule)) return false;
    SceneState& state = SceneAccess::State(scene);
    state.componentStorage.MotionSkeletonRules().Set(entity, rule);
    MarkScenePrefabNodeDirty(state, entity);
    return true;
}
void SceneComponentMutationService::RemoveMotionSkeletonRule(Scene& scene, SceneEntity entity) noexcept {
    if (!SceneEntityService::IsAlive(scene, entity)) return;
    SceneState& state = SceneAccess::State(scene);
    state.componentStorage.MotionSkeletonRules().Remove(entity);
    MarkScenePrefabNodeDirty(state, entity);
}
void SceneComponentMutationService::MarkMotionSkeletonRuleModified(Scene& scene, SceneEntity entity) noexcept {
    if (!SceneEntityService::IsAlive(scene, entity)) return;
    SceneState& state = SceneAccess::State(scene);
    state.componentStorage.MotionSkeletonRules().MarkModified(entity);
    MarkScenePrefabNodeDirty(state, entity);
}

} // namespace kb::scene
