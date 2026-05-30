#include "engine/scene/Scene.hpp"

#include "scene/components/SceneComponentStorage.hpp"

namespace kb::scene {

TransformComponent Scene::Transform(SceneObject object) const {
    return IsAlive(object) ? Transform(object.Entity()) : TransformComponent{};
}

TransformComponent Scene::Transform(SceneEntity entity) const {
    const TransformComponent* transform = TryGetTransform(entity);
    return transform == nullptr ? TransformComponent{} : *transform;
}

const TransformComponent* Scene::TryGetTransform(SceneEntity entity) const noexcept {
    return IsAlive(entity) ? componentStorage_->TryGetTransform(entity) : nullptr;
}

TransformComponent* Scene::TryGetTransform(SceneEntity entity) noexcept {
    return IsAlive(entity) ? componentStorage_->TryGetTransform(entity) : nullptr;
}

void Scene::SetTransform(SceneObject object, const TransformComponent& transform) {
    if (IsAlive(object)) {
        SetTransform(object.Entity(), transform);
    }
}

void Scene::SetTransform(SceneEntity entity, const TransformComponent& transform) {
    if (IsAlive(entity)) {
        componentStorage_->SetTransform(entity, transform);
    }
}

void Scene::MarkTransformModified(SceneEntity entity) noexcept {
    if (IsAlive(entity)) {
        componentStorage_->MarkTransformModified(entity);
    }
}

} // namespace kb::scene
