#include "scene/components/SceneComponentStorage.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentRegistry.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

bool SceneComponentStorage::HasMeshRenderer(SceneEntity entity) const noexcept {
    return SceneComponentAccess::Has(world_, entity, components_.MeshRendererComponentId());
}

const MeshRendererComponent* SceneComponentStorage::TryGetMeshRenderer(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<MeshRendererComponent>(world_, entity, components_.MeshRendererComponentId());
}

MeshRendererComponent* SceneComponentStorage::TryGetMeshRenderer(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<MeshRendererComponent>(world_, entity, components_.MeshRendererComponentId());
}

void SceneComponentStorage::SetMeshRenderer(SceneEntity entity, const MeshRendererComponent& renderer) {
    SceneComponentStorageAccess::Set(world_, entity, components_.MeshRendererComponentId(), renderer);
}

void SceneComponentStorage::RemoveMeshRenderer(SceneEntity entity) noexcept {
    SceneComponentAccess::Remove(world_, entity, components_.MeshRendererComponentId());
}

void SceneComponentStorage::MarkMeshRendererModified(SceneEntity entity) noexcept {
    SceneComponentAccess::MarkModified(world_, entity, components_.MeshRendererComponentId());
}

} // namespace kb::scene
