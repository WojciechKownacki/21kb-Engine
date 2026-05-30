#include "scene/components/SceneCameraComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneCameraComponentStore::SceneCameraComponentStore(ecs_world_t* world, std::uint64_t componentId) noexcept
    : world_(world)
    , componentId_(componentId) {}

bool SceneCameraComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentAccess::Has(world_, entity, componentId_);
}

const CameraComponent* SceneCameraComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<CameraComponent>(world_, entity, componentId_);
}

CameraComponent* SceneCameraComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<CameraComponent>(world_, entity, componentId_);
}

void SceneCameraComponentStore::Set(SceneEntity entity, const CameraComponent& camera) {
    SceneComponentStorageAccess::Set(world_, entity, componentId_, camera);
}

void SceneCameraComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentAccess::Remove(world_, entity, componentId_);
}

void SceneCameraComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentAccess::MarkModified(world_, entity, componentId_);
}

} // namespace kb::scene
