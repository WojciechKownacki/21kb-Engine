#include "engine/scene/SceneLensEchoComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasLensEcho(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.LensEchoes().Has(entity); }
const LensEchoComponent* SceneComponentQueryService::TryGetLensEcho(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.LensEchoes().TryGet(entity) : nullptr; }
LensEchoComponent* SceneComponentMutationService::TryGetLensEcho(Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.LensEchoes().TryGet(entity) : nullptr; }
void SceneComponentMutationService::SetLensEcho(Scene& scene, SceneEntity entity, const LensEchoComponent& component) { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.LensEchoes().Set(entity, component); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::RemoveLensEcho(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.LensEchoes().Remove(entity); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::MarkLensEchoModified(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.LensEchoes().MarkModified(entity); MarkScenePrefabNodeDirty(state, entity); } }

SceneLensEchoComponentQueries::SceneLensEchoComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneLensEchoComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasLensEcho(scene_, entity); }
const LensEchoComponent* SceneLensEchoComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetLensEcho(scene_, entity); }
void SceneLensEchoComponentQueries::ForEach(LensEchoVisitor visitor, void* context) const { SceneAccess::State(scene_).componentStorage.LensEchoes().ForEach(visitor, context); }
SceneLensEchoComponents::SceneLensEchoComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneLensEchoComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasLensEcho(scene_, entity); }
const LensEchoComponent* SceneLensEchoComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetLensEcho(scene_, entity); }
LensEchoComponent* SceneLensEchoComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetLensEcho(scene_, entity); }
void SceneLensEchoComponents::Set(SceneEntity entity, const LensEchoComponent& component) { SceneComponentMutationService::SetLensEcho(scene_, entity, component); }
void SceneLensEchoComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveLensEcho(scene_, entity); }
void SceneLensEchoComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkLensEchoModified(scene_, entity); }

} // namespace kb::scene
