#include "scene/components/SceneCameraComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneCameraComponentStore::SceneCameraComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world) {
    static_cast<void>(componentId);
}

bool SceneCameraComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<CameraComponent>(world_, entity);
}

const CameraComponent* SceneCameraComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<CameraComponent>(world_, entity);
}

CameraComponent* SceneCameraComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<CameraComponent>(world_, entity);
}

void SceneCameraComponentStore::Set(SceneEntity entity, const CameraComponent& camera) {
    SceneComponentStorageAccess::Set<CameraComponent>(world_, entity, camera);
}

void SceneCameraComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<CameraComponent>(world_, entity);
}

void SceneCameraComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<CameraComponent>(world_, entity);
}

} // namespace kb::scene
