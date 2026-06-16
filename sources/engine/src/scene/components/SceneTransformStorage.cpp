#include "scene/components/SceneTransformComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

#include <algorithm>

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
    const TransformComponent* current = TryGet(entity);
    TransformComponent stored = transform;
    stored.localVersion = current == nullptr ? std::max<std::uint64_t>(stored.localVersion, 1ULL) : current->localVersion + 1U;
    stored.parentVersion = current == nullptr ? 0U : current->parentVersion;
    stored.worldVersion = current == nullptr ? 0U : current->worldVersion;
    stored.worldDirty = true;
    SceneComponentStorageAccess::Set(world_, entity, componentId_, stored);
}

void SceneTransformComponentStore::MarkModified(SceneEntity entity) noexcept {
    if (TransformComponent* transform = TryGet(entity); transform != nullptr) {
        ++transform->localVersion;
        transform->worldDirty = true;
    }
    SceneComponentAccess::MarkModified(world_, entity, componentId_);
}

void SceneTransformComponentStore::MarkParentModified(SceneEntity entity) noexcept {
    if (TransformComponent* transform = TryGet(entity); transform != nullptr) {
        transform->worldDirty = true;
    }
    SceneComponentAccess::MarkModified(world_, entity, componentId_);
}

} // namespace kb::scene
