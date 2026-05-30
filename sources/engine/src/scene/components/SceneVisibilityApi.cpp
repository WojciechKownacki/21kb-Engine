#include "engine/scene/Scene.hpp"

#include "scene/components/SceneComponentStorage.hpp"

namespace kb::scene {

VisibilityComponent Scene::Visibility(SceneEntity entity) const {
    const VisibilityComponent* visibility = TryGetVisibility(entity);
    return visibility == nullptr ? VisibilityComponent{} : *visibility;
}

const VisibilityComponent* Scene::TryGetVisibility(SceneEntity entity) const noexcept {
    return IsAlive(entity) ? componentStorage_->TryGetVisibility(entity) : nullptr;
}

VisibilityComponent* Scene::TryGetVisibility(SceneEntity entity) noexcept {
    return IsAlive(entity) ? componentStorage_->TryGetVisibility(entity) : nullptr;
}

void Scene::SetVisibility(SceneEntity entity, const VisibilityComponent& visibility) {
    if (IsAlive(entity)) {
        componentStorage_->SetVisibility(entity, visibility);
    }
}

void Scene::MarkVisibilityModified(SceneEntity entity) noexcept {
    if (IsAlive(entity)) {
        componentStorage_->MarkVisibilityModified(entity);
    }
}

} // namespace kb::scene
