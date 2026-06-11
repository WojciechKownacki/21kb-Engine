#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {

SceneAudioSourceComponentQueries::SceneAudioSourceComponentQueries(const Scene& scene) noexcept
    : scene_(scene) {}

bool SceneAudioSourceComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasAudioSource(scene_, entity);
}

const AudioSourceComponent* SceneAudioSourceComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetAudioSource(scene_, entity);
}

SceneAudioSourceComponents::SceneAudioSourceComponents(Scene& scene) noexcept
    : scene_(scene) {}

bool SceneAudioSourceComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasAudioSource(scene_, entity);
}

const AudioSourceComponent* SceneAudioSourceComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetAudioSource(scene_, entity);
}

AudioSourceComponent* SceneAudioSourceComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetAudioSource(scene_, entity);
}

void SceneAudioSourceComponents::Set(SceneEntity entity, const AudioSourceComponent& audioSource) {
    SceneComponentMutationService::SetAudioSource(scene_, entity, audioSource);
}

void SceneAudioSourceComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemoveAudioSource(scene_, entity);
}

void SceneAudioSourceComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkAudioSourceModified(scene_, entity);
}

} // namespace kb::scene
