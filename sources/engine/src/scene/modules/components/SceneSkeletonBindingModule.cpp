#include "engine/scene/SceneSkeletonBindingComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {

SceneSkeletonBindingComponentQueries::SceneSkeletonBindingComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneSkeletonBindingComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasSkeletonBinding(scene_, entity); }
const SkeletonBindingComponent* SceneSkeletonBindingComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetSkeletonBinding(scene_, entity); }

SceneSkeletonBindingComponents::SceneSkeletonBindingComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneSkeletonBindingComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasSkeletonBinding(scene_, entity); }
const SkeletonBindingComponent* SceneSkeletonBindingComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetSkeletonBinding(scene_, entity); }
SkeletonBindingComponent* SceneSkeletonBindingComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetSkeletonBinding(scene_, entity); }
bool SceneSkeletonBindingComponents::Set(SceneEntity entity, const SkeletonBindingComponent& binding) { return SceneComponentMutationService::SetSkeletonBinding(scene_, entity, binding); }
void SceneSkeletonBindingComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveSkeletonBinding(scene_, entity); }
void SceneSkeletonBindingComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkSkeletonBindingModified(scene_, entity); }

} // namespace kb::scene
