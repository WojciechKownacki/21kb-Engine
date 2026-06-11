#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"

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
        SceneAccess::State(scene).componentStorage.AudioSources().Set(entity, audioSource);
    }
}

void SceneComponentMutationService::RemoveAudioSource(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.AudioSources().Remove(entity);
    }
}

void SceneComponentMutationService::MarkAudioSourceModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.AudioSources().MarkModified(entity);
    }
}

} // namespace kb::scene
