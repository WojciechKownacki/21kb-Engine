#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasInput(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.Inputs().Has(entity);
}

const InputComponent* SceneComponentQueryService::TryGetInput(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Inputs().TryGet(entity) : nullptr;
}

InputComponent* SceneComponentMutationService::TryGetInput(Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Inputs().TryGet(entity) : nullptr;
}

void SceneComponentMutationService::SetInput(Scene& scene, SceneEntity entity, const InputComponent& input) {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Inputs().Set(entity, input);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

void SceneComponentMutationService::RemoveInput(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Inputs().Remove(entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

void SceneComponentMutationService::MarkInputModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Inputs().MarkModified(entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

} // namespace kb::scene
