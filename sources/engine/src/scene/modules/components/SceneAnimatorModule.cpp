#include "engine/scene/SceneAnimatorComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {

SceneAnimatorComponentQueries::SceneAnimatorComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneAnimatorComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasAnimator(scene_, entity); }
const Animator* SceneAnimatorComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetAnimator(scene_, entity); }

SceneAnimatorComponents::SceneAnimatorComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneAnimatorComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasAnimator(scene_, entity); }
const Animator* SceneAnimatorComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetAnimator(scene_, entity); }
Animator* SceneAnimatorComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetAnimator(scene_, entity); }
void SceneAnimatorComponents::Set(SceneEntity entity, const Animator& animator) { SceneComponentMutationService::SetAnimator(scene_, entity, animator); }
void SceneAnimatorComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveAnimator(scene_, entity); }
void SceneAnimatorComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkAnimatorModified(scene_, entity); }

} // namespace kb::scene
