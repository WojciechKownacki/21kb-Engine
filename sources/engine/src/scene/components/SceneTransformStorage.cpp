#include "scene/components/SceneTransformComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneTransformComponentStore::SceneTransformComponentStore(ecs_world_t* world, std::uint64_t componentId) noexcept
    : world_(world)
    , componentId_(componentId) {}

const TransformComponent* SceneTransformComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<TransformComponent>(world_, entity, componentId_);
}

TransformComponent* SceneTransformComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<TransformComponent>(world_, entity, componentId_);
}

void SceneTransformComponentStore::Set(SceneEntity entity, const TransformComponent& transform) {
    TransformComponent stored = transform;
    stored.worldDirty = true;
    SceneComponentStorageAccess::Set(world_, entity, componentId_, stored);
}

void SceneTransformComponentStore::MarkModified(SceneEntity entity) noexcept {
    if (TransformComponent* transform = TryGet(entity); transform != nullptr) {
        transform->worldDirty = true;
    }
    SceneComponentAccess::MarkModified(world_, entity, componentId_);
}

} // namespace kb::scene
