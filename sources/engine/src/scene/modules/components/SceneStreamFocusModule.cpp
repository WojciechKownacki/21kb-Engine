#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {
SceneStreamFocusComponentQueries::SceneStreamFocusComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneStreamFocusComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasStreamFocus(scene_, entity); }
const StreamFocusComponent* SceneStreamFocusComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetStreamFocus(scene_, entity); }
SceneStreamFocusComponents::SceneStreamFocusComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneStreamFocusComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasStreamFocus(scene_, entity); }
const StreamFocusComponent* SceneStreamFocusComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetStreamFocus(scene_, entity); }
StreamFocusComponent* SceneStreamFocusComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetStreamFocus(scene_, entity); }
void SceneStreamFocusComponents::Set(SceneEntity entity, const StreamFocusComponent& component) { SceneComponentMutationService::SetStreamFocus(scene_, entity, component); }
void SceneStreamFocusComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveStreamFocus(scene_, entity); }
void SceneStreamFocusComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkStreamFocusModified(scene_, entity); }
} // namespace kb::scene
