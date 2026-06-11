#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {

SceneAudioListenerComponentQueries::SceneAudioListenerComponentQueries(const Scene& scene) noexcept
    : scene_(scene) {}

bool SceneAudioListenerComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasAudioListener(scene_, entity);
}

const AudioListenerComponent* SceneAudioListenerComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetAudioListener(scene_, entity);
}

SceneAudioListenerComponents::SceneAudioListenerComponents(Scene& scene) noexcept
    : scene_(scene) {}

bool SceneAudioListenerComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasAudioListener(scene_, entity);
}

const AudioListenerComponent* SceneAudioListenerComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetAudioListener(scene_, entity);
}

AudioListenerComponent* SceneAudioListenerComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetAudioListener(scene_, entity);
}

void SceneAudioListenerComponents::Set(SceneEntity entity, const AudioListenerComponent& audioListener) {
    SceneComponentMutationService::SetAudioListener(scene_, entity, audioListener);
}

void SceneAudioListenerComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemoveAudioListener(scene_, entity);
}

void SceneAudioListenerComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkAudioListenerModified(scene_, entity);
}

} // namespace kb::scene
