#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasAudioListener(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.AudioListeners().Has(entity);
}

const AudioListenerComponent* SceneComponentQueryService::TryGetAudioListener(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.AudioListeners().TryGet(entity) : nullptr;
}

AudioListenerComponent* SceneComponentMutationService::TryGetAudioListener(Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.AudioListeners().TryGet(entity) : nullptr;
}

void SceneComponentMutationService::SetAudioListener(Scene& scene, SceneEntity entity, const AudioListenerComponent& audioListener) {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.AudioListeners().Set(entity, audioListener);
    }
}

void SceneComponentMutationService::RemoveAudioListener(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.AudioListeners().Remove(entity);
    }
}

void SceneComponentMutationService::MarkAudioListenerModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.AudioListeners().MarkModified(entity);
    }
}

} // namespace kb::scene
