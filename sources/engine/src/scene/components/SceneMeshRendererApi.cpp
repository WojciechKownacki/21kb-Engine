#include "engine/scene/Scene.hpp"

#include "scene/components/SceneComponentStorage.hpp"

namespace kb::scene {

bool Scene::HasMeshRenderer(SceneEntity entity) const noexcept {
    return IsAlive(entity) && componentStorage_->HasMeshRenderer(entity);
}

const MeshRendererComponent* Scene::TryGetMeshRenderer(SceneEntity entity) const noexcept {
    return IsAlive(entity) ? componentStorage_->TryGetMeshRenderer(entity) : nullptr;
}

MeshRendererComponent* Scene::TryGetMeshRenderer(SceneEntity entity) noexcept {
    return IsAlive(entity) ? componentStorage_->TryGetMeshRenderer(entity) : nullptr;
}

void Scene::SetMeshRenderer(SceneEntity entity, const MeshRendererComponent& renderer) {
    if (IsAlive(entity)) {
        componentStorage_->SetMeshRenderer(entity, renderer);
    }
}

void Scene::RemoveMeshRenderer(SceneEntity entity) noexcept {
    if (IsAlive(entity)) {
        componentStorage_->RemoveMeshRenderer(entity);
    }
}

void Scene::MarkMeshRendererModified(SceneEntity entity) noexcept {
    if (IsAlive(entity)) {
        componentStorage_->MarkMeshRendererModified(entity);
    }
}

} // namespace kb::scene
