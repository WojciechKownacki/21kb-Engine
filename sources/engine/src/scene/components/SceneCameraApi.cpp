#include "engine/scene/Scene.hpp"

#include "scene/components/SceneComponentStorage.hpp"

namespace kb::scene {

bool Scene::HasCamera(SceneEntity entity) const noexcept {
    return IsAlive(entity) && componentStorage_->HasCamera(entity);
}

const CameraComponent* Scene::TryGetCamera(SceneEntity entity) const noexcept {
    return IsAlive(entity) ? componentStorage_->TryGetCamera(entity) : nullptr;
}

CameraComponent* Scene::TryGetCamera(SceneEntity entity) noexcept {
    return IsAlive(entity) ? componentStorage_->TryGetCamera(entity) : nullptr;
}

void Scene::SetCamera(SceneEntity entity, const CameraComponent& camera) {
    if (IsAlive(entity)) {
        componentStorage_->SetCamera(entity, camera);
    }
}

void Scene::RemoveCamera(SceneEntity entity) noexcept {
    if (IsAlive(entity)) {
        componentStorage_->RemoveCamera(entity);
    }
}

void Scene::MarkCameraModified(SceneEntity entity) noexcept {
    if (IsAlive(entity)) {
        componentStorage_->MarkCameraModified(entity);
    }
}

} // namespace kb::scene
