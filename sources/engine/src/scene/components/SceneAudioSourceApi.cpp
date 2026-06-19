#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasAudioSource(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.AudioSources().Has(entity);
}

const AudioSourceComponent* SceneComponentQueryService::TryGetAudioSource(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.AudioSources().TryGet(entity) : nullptr;
}

AudioSourceComponent* SceneComponentMutationService::TryGetAudioSource(Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.AudioSources().TryGet(entity) : nullptr;
}

void SceneComponentMutationService::SetAudioSource(Scene& scene, SceneEntity entity, const AudioSourceComponent& audioSource) {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.AudioSources().Set(entity, audioSource);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

void SceneComponentMutationService::RemoveAudioSource(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.AudioSources().Remove(entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

void SceneComponentMutationService::MarkAudioSourceModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.AudioSources().MarkModified(entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

} // namespace kb::scene
