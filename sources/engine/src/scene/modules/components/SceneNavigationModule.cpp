#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {

#define KB_NAVIGATION_MODULE(Name, Type) \
Scene##Name##ComponentQueries::Scene##Name##ComponentQueries(const Scene& scene) noexcept : scene_(scene) {} \
bool Scene##Name##ComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::Has##Name(scene_, entity); } \
const Type* Scene##Name##ComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGet##Name(scene_, entity); } \
Scene##Name##Components::Scene##Name##Components(Scene& scene) noexcept : scene_(scene) {} \
bool Scene##Name##Components::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::Has##Name(scene_, entity); } \
const Type* Scene##Name##Components::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGet##Name(scene_, entity); } \
Type* Scene##Name##Components::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGet##Name(scene_, entity); } \
void Scene##Name##Components::Set(SceneEntity entity, const Type& component) { SceneComponentMutationService::Set##Name(scene_, entity, component); } \
void Scene##Name##Components::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::Remove##Name(scene_, entity); } \
void Scene##Name##Components::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::Mark##Name##Modified(scene_, entity); }

KB_NAVIGATION_MODULE(NavAgent, NavAgent)
KB_NAVIGATION_MODULE(NavObstacle, NavObstacle)

#undef KB_NAVIGATION_MODULE

} // namespace kb::scene
