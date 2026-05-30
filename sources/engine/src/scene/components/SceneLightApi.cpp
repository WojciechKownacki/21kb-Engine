#include "engine/scene/Scene.hpp"

#include "scene/components/SceneComponentStorage.hpp"

namespace kb::scene {

bool Scene::HasLight(SceneEntity entity) const noexcept {
    return IsAlive(entity) && componentStorage_->HasLight(entity);
}

const LightComponent* Scene::TryGetLight(SceneEntity entity) const noexcept {
    return IsAlive(entity) ? componentStorage_->TryGetLight(entity) : nullptr;
}

LightComponent* Scene::TryGetLight(SceneEntity entity) noexcept {
    return IsAlive(entity) ? componentStorage_->TryGetLight(entity) : nullptr;
}

void Scene::SetLight(SceneEntity entity, const LightComponent& light) {
    if (IsAlive(entity)) {
        componentStorage_->SetLight(entity, light);
    }
}

void Scene::RemoveLight(SceneEntity entity) noexcept {
    if (IsAlive(entity)) {
        componentStorage_->RemoveLight(entity);
    }
}

void Scene::MarkLightModified(SceneEntity entity) noexcept {
    if (IsAlive(entity)) {
        componentStorage_->MarkLightModified(entity);
    }
}

} // namespace kb::scene
