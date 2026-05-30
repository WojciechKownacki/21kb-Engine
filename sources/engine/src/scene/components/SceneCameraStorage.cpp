#include "scene/components/SceneComponentStorage.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentRegistry.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

bool SceneComponentStorage::HasCamera(SceneEntity entity) const noexcept {
    return SceneComponentAccess::Has(world_, entity, components_.CameraComponentId());
}

const CameraComponent* SceneComponentStorage::TryGetCamera(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<CameraComponent>(world_, entity, components_.CameraComponentId());
}

CameraComponent* SceneComponentStorage::TryGetCamera(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<CameraComponent>(world_, entity, components_.CameraComponentId());
}

void SceneComponentStorage::SetCamera(SceneEntity entity, const CameraComponent& camera) {
    SceneComponentStorageAccess::Set(world_, entity, components_.CameraComponentId(), camera);
}

void SceneComponentStorage::RemoveCamera(SceneEntity entity) noexcept {
    SceneComponentAccess::Remove(world_, entity, components_.CameraComponentId());
}

void SceneComponentStorage::MarkCameraModified(SceneEntity entity) noexcept {
    SceneComponentAccess::MarkModified(world_, entity, components_.CameraComponentId());
}

} // namespace kb::scene
