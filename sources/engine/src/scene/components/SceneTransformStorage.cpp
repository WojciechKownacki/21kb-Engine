#include "scene/components/SceneTransformComponentStore.hpp"

#include "ecs/world/WorldInternalAccess.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

#include <algorithm>

namespace kb::scene {

SceneTransformComponentStore::SceneTransformComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world), componentId_(componentId) {}

const TransformComponent* SceneTransformComponentStore::TryGet(SceneEntity entity) const noexcept {
    return entity.IsValid() ? static_cast<const TransformComponent*>(kb::ecs::WorldInternalAccess::TryGetComponent(*world_, entity, componentId_)) : nullptr;
}

TransformComponent* SceneTransformComponentStore::TryGet(SceneEntity entity) noexcept {
    return entity.IsValid() ? static_cast<TransformComponent*>(kb::ecs::WorldInternalAccess::TryGetMutableComponent(*world_, entity, componentId_)) : nullptr;
}

void SceneTransformComponentStore::Set(SceneEntity entity, const TransformComponent& transform) {
    const TransformComponent* current = TryGet(entity);
    TransformComponent stored = transform;
    stored.localVersion = current == nullptr ? std::max<std::uint64_t>(stored.localVersion, 1ULL) : current->localVersion + 1U;
    stored.parentVersion = current == nullptr ? 0U : current->parentVersion;
    stored.worldVersion = current == nullptr ? 0U : current->worldVersion;
    stored.worldDirty = true;
    SceneComponentStorageAccess::Set<TransformComponent>(world_, entity, stored);
}

void SceneTransformComponentStore::MarkModified(SceneEntity entity) noexcept {
    if (TransformComponent* transform = TryGet(entity); transform != nullptr) {
        ++transform->localVersion;
        transform->worldDirty = true;
    }
    if (entity.IsValid()) kb::ecs::WorldInternalAccess::MarkComponentModified(*world_, entity, componentId_);
}

void SceneTransformComponentStore::MarkParentModified(SceneEntity entity) noexcept {
    if (TransformComponent* transform = TryGet(entity); transform != nullptr) {
        transform->worldDirty = true;
    }
    if (entity.IsValid()) kb::ecs::WorldInternalAccess::MarkComponentModified(*world_, entity, componentId_);
}

} // namespace kb::scene
