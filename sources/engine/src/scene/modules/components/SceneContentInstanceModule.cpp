#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {
SceneContentInstanceComponentQueries::SceneContentInstanceComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneContentInstanceComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasContentInstance(scene_, entity); }
const ContentInstanceComponent* SceneContentInstanceComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetContentInstance(scene_, entity); }
SceneContentInstanceComponents::SceneContentInstanceComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneContentInstanceComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasContentInstance(scene_, entity); }
const ContentInstanceComponent* SceneContentInstanceComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetContentInstance(scene_, entity); }
ContentInstanceComponent* SceneContentInstanceComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetContentInstance(scene_, entity); }
void SceneContentInstanceComponents::Set(SceneEntity entity, const ContentInstanceComponent& component) { SceneComponentMutationService::SetContentInstance(scene_, entity, component); }
void SceneContentInstanceComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveContentInstance(scene_, entity); }
void SceneContentInstanceComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkContentInstanceModified(scene_, entity); }
} // namespace kb::scene
