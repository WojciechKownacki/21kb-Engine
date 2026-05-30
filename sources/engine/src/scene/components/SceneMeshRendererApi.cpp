#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasMeshRenderer(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.MeshRenderers().Has(entity);
}

const MeshRendererComponent* SceneComponentQueryService::TryGetMeshRenderer(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.MeshRenderers().TryGet(entity) : nullptr;
}

MeshRendererComponent* SceneComponentMutationService::TryGetMeshRenderer(Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.MeshRenderers().TryGet(entity) : nullptr;
}

void SceneComponentMutationService::SetMeshRenderer(Scene& scene, SceneEntity entity, const MeshRendererComponent& renderer) {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.MeshRenderers().Set(entity, renderer);
    }
}

void SceneComponentMutationService::RemoveMeshRenderer(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.MeshRenderers().Remove(entity);
    }
}

void SceneComponentMutationService::MarkMeshRendererModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.MeshRenderers().MarkModified(entity);
    }
}

} // namespace kb::scene
