#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {

SceneCameraComponentQueries::SceneCameraComponentQueries(const Scene& scene) noexcept
    : scene_(scene) {}

bool SceneCameraComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasCamera(scene_, entity);
}

const CameraComponent* SceneCameraComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetCamera(scene_, entity);
}

SceneCameraComponents::SceneCameraComponents(Scene& scene) noexcept
    : scene_(scene) {}

bool SceneCameraComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasCamera(scene_, entity);
}

const CameraComponent* SceneCameraComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetCamera(scene_, entity);
}

CameraComponent* SceneCameraComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetCamera(scene_, entity);
}

void SceneCameraComponents::Set(SceneEntity entity, const CameraComponent& camera) {
    SceneComponentMutationService::SetCamera(scene_, entity, camera);
}

void SceneCameraComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemoveCamera(scene_, entity);
}

void SceneCameraComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkCameraModified(scene_, entity);
}

} // namespace kb::scene
