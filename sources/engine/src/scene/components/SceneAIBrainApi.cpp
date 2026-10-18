#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneHistoryService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasAIBrain(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.AIBrains().Has(entity);
}
const AIBrainComponent* SceneComponentQueryService::TryGetAIBrain(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.AIBrains().TryGet(entity) : nullptr;
}
AIBrainComponent* SceneComponentMutationService::TryGetAIBrain(Scene& scene, SceneEntity entity) noexcept {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.AIBrains().TryGet(entity) : nullptr;
}
bool SceneComponentMutationService::SetAIBrain(Scene& scene, SceneEntity entity, const AIBrainComponent& aiBrain) {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    if (!SceneEntityService::IsAlive(scene, entity) || !IsAIBrainComponentValid(aiBrain)) return false;
    SceneState& state = SceneAccess::State(scene);
    state.componentStorage.AIBrains().Set(entity, aiBrain);
    MarkScenePrefabNodeDirty(state, entity);
    return true;
}
void SceneComponentMutationService::RemoveAIBrain(Scene& scene, SceneEntity entity) noexcept {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    if (!SceneEntityService::IsAlive(scene, entity)) return;
    SceneState& state = SceneAccess::State(scene);
    state.componentStorage.AIBrains().Remove(entity);
    MarkScenePrefabNodeDirty(state, entity);
}
void SceneComponentMutationService::MarkAIBrainModified(Scene& scene, SceneEntity entity) noexcept {
    if (!SceneEntityService::IsAlive(scene, entity)) return;
    SceneState& state = SceneAccess::State(scene);
    state.componentStorage.AIBrains().MarkModified(entity);
    MarkScenePrefabNodeDirty(state, entity);
}

} // namespace kb::scene
