#include "engine/scene/SceneMotionSkeletonRuleComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {

SceneMotionSkeletonRuleComponentQueries::SceneMotionSkeletonRuleComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneMotionSkeletonRuleComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasMotionSkeletonRule(scene_, entity); }
const MotionSkeletonRuleComponent* SceneMotionSkeletonRuleComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetMotionSkeletonRule(scene_, entity); }

SceneMotionSkeletonRuleComponents::SceneMotionSkeletonRuleComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneMotionSkeletonRuleComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasMotionSkeletonRule(scene_, entity); }
const MotionSkeletonRuleComponent* SceneMotionSkeletonRuleComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetMotionSkeletonRule(scene_, entity); }
MotionSkeletonRuleComponent* SceneMotionSkeletonRuleComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetMotionSkeletonRule(scene_, entity); }
bool SceneMotionSkeletonRuleComponents::Set(SceneEntity entity, const MotionSkeletonRuleComponent& rule) { return SceneComponentMutationService::SetMotionSkeletonRule(scene_, entity, rule); }
void SceneMotionSkeletonRuleComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveMotionSkeletonRule(scene_, entity); }
void SceneMotionSkeletonRuleComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkMotionSkeletonRuleModified(scene_, entity); }

} // namespace kb::scene
