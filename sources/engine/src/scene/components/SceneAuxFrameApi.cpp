#include "engine/scene/SceneAuxFrameComponents.hpp"
#include "engine/scene/SceneAuxFrameQueries.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasAuxFrame(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.AuxFrames().Has(entity); }
const AuxFrameComponent* SceneComponentQueryService::TryGetAuxFrame(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.AuxFrames().TryGet(entity) : nullptr; }
AuxFrameComponent* SceneComponentMutationService::TryGetAuxFrame(Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.AuxFrames().TryGet(entity) : nullptr; }
void SceneComponentMutationService::SetAuxFrame(Scene& scene, SceneEntity entity, const AuxFrameComponent& component) { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.AuxFrames().Set(entity, component); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::RemoveAuxFrame(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.AuxFrames().Remove(entity); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::MarkAuxFrameModified(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.AuxFrames().MarkModified(entity); MarkScenePrefabNodeDirty(state, entity); } }

SceneAuxFrameComponentQueries::SceneAuxFrameComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneAuxFrameComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasAuxFrame(scene_, entity); }
const AuxFrameComponent* SceneAuxFrameComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetAuxFrame(scene_, entity); }
void SceneAuxFrameComponentQueries::ForEach(AuxFrameVisitor visitor, void* context) const { SceneAccess::State(scene_).componentStorage.AuxFrames().ForEach(visitor, context); }
SceneAuxFrameComponents::SceneAuxFrameComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneAuxFrameComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasAuxFrame(scene_, entity); }
const AuxFrameComponent* SceneAuxFrameComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetAuxFrame(scene_, entity); }
AuxFrameComponent* SceneAuxFrameComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetAuxFrame(scene_, entity); }
void SceneAuxFrameComponents::Set(SceneEntity entity, const AuxFrameComponent& component) { SceneComponentMutationService::SetAuxFrame(scene_, entity, component); }
void SceneAuxFrameComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveAuxFrame(scene_, entity); }
void SceneAuxFrameComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkAuxFrameModified(scene_, entity); }

bool SceneAuxFrameIsRenderable(const Scene& scene, SceneEntity entity) noexcept {
    const AuxFrameComponent* frame = scene.Components().AuxFrames().TryGet(entity);
    return frame != nullptr && frame->enabled && IsAuxFrameComponentValid(*frame) && scene.Components().Cameras().Has(entity);
}

} // namespace kb::scene
