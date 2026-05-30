#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {

SceneMeshRendererComponentQueries::SceneMeshRendererComponentQueries(const Scene& scene) noexcept
    : scene_(scene) {}

bool SceneMeshRendererComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasMeshRenderer(scene_, entity);
}

const MeshRendererComponent* SceneMeshRendererComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetMeshRenderer(scene_, entity);
}

SceneMeshRendererComponents::SceneMeshRendererComponents(Scene& scene) noexcept
    : scene_(scene) {}

bool SceneMeshRendererComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasMeshRenderer(scene_, entity);
}

const MeshRendererComponent* SceneMeshRendererComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetMeshRenderer(scene_, entity);
}

MeshRendererComponent* SceneMeshRendererComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetMeshRenderer(scene_, entity);
}

void SceneMeshRendererComponents::Set(SceneEntity entity, const MeshRendererComponent& renderer) {
    SceneComponentMutationService::SetMeshRenderer(scene_, entity, renderer);
}

void SceneMeshRendererComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemoveMeshRenderer(scene_, entity);
}

void SceneMeshRendererComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkMeshRendererModified(scene_, entity);
}

} // namespace kb::scene
