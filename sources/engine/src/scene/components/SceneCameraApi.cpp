#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasCamera(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.Cameras().Has(entity);
}

const CameraComponent* SceneComponentQueryService::TryGetCamera(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Cameras().TryGet(entity) : nullptr;
}

CameraComponent* SceneComponentMutationService::TryGetCamera(Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Cameras().TryGet(entity) : nullptr;
}

void SceneComponentMutationService::SetCamera(Scene& scene, SceneEntity entity, const CameraComponent& camera) {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.Cameras().Set(entity, camera);
    }
}

void SceneComponentMutationService::RemoveCamera(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.Cameras().Remove(entity);
    }
}

void SceneComponentMutationService::MarkCameraModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.Cameras().MarkModified(entity);
    }
}

} // namespace kb::scene
