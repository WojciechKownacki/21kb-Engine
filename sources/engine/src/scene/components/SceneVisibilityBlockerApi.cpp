#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasVisibilityBlocker(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.VisibilityBlockers().Has(entity); }
const SceneVisibilityBlockerComponent* SceneComponentQueryService::TryGetVisibilityBlocker(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.VisibilityBlockers().TryGet(entity) : nullptr; }
SceneVisibilityBlockerComponent* SceneComponentMutationService::TryGetVisibilityBlocker(Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.VisibilityBlockers().TryGet(entity) : nullptr; }
void SceneComponentMutationService::SetVisibilityBlocker(Scene& scene, SceneEntity entity, const SceneVisibilityBlockerComponent& component) { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.VisibilityBlockers().Set(entity, component); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::RemoveVisibilityBlocker(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.VisibilityBlockers().Remove(entity); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::MarkVisibilityBlockerModified(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.VisibilityBlockers().MarkModified(entity); MarkScenePrefabNodeDirty(state, entity); } }

SceneVisibilityBlockerComponentQueries::SceneVisibilityBlockerComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneVisibilityBlockerComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasVisibilityBlocker(scene_, entity); }
const SceneVisibilityBlockerComponent* SceneVisibilityBlockerComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetVisibilityBlocker(scene_, entity); }
SceneVisibilityBlockerComponents::SceneVisibilityBlockerComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneVisibilityBlockerComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasVisibilityBlocker(scene_, entity); }
const SceneVisibilityBlockerComponent* SceneVisibilityBlockerComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetVisibilityBlocker(scene_, entity); }
SceneVisibilityBlockerComponent* SceneVisibilityBlockerComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetVisibilityBlocker(scene_, entity); }
void SceneVisibilityBlockerComponents::Set(SceneEntity entity, const SceneVisibilityBlockerComponent& component) { SceneComponentMutationService::SetVisibilityBlocker(scene_, entity, component); }
void SceneVisibilityBlockerComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveVisibilityBlocker(scene_, entity); }
void SceneVisibilityBlockerComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkVisibilityBlockerModified(scene_, entity); }

} // namespace kb::scene
