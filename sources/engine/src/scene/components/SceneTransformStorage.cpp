#include "scene/components/SceneTransformComponentStore.hpp"
#include "engine/ecs/NativeArchetypeStorage.hpp"

#include "ecs/world/WorldInternalAccess.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

#include <algorithm>

namespace kb::scene {

SceneTransformComponentStore::SceneTransformComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world), componentId_(componentId) {}

const TransformComponent* SceneTransformComponentStore::TryGet(SceneEntity entity) const noexcept {
    if (const void* data = world_->NativeStorage().TryGetComponentData(entity, componentId_); data != nullptr) {
        return static_cast<const TransformComponent*>(data);
    }
    if (!world_->IsAlive(entity)) return nullptr;
    return static_cast<const TransformComponent*>(kb::ecs::WorldInternalAccess::TryGetComponent(*world_, entity, componentId_));
}

TransformComponent* SceneTransformComponentStore::TryGet(SceneEntity entity) noexcept {
    if (void* data = kb::ecs::WorldInternalAccess::TryGetMutableNativeComponent(*world_, entity, componentId_); data != nullptr) {
        return static_cast<TransformComponent*>(data);
    }
    if (!world_->IsAlive(entity)) return nullptr;
    return static_cast<TransformComponent*>(kb::ecs::WorldInternalAccess::TryGetMutableComponent(*world_, entity, componentId_));
}

void SceneTransformComponentStore::Set(SceneEntity entity, const TransformComponent& transform) {
    const auto bumpVersions = [&transform](const TransformComponent* current) {
        TransformComponent stored = transform;
        stored.localVersion = current == nullptr ? std::max<std::uint64_t>(stored.localVersion, 1ULL) : current->localVersion + 1U;
        stored.parentVersion = current == nullptr ? 0U : current->parentVersion;
        stored.worldVersion = current == nullptr ? 0U : current->worldVersion;
        stored.worldDirty = true;
        return stored;
    };
    // Nothing observes transforms: one lookup finds the row and flags it.
    if (!world_->MirrorsValueWrites(componentId_)) {
        if (void* data = kb::ecs::WorldInternalAccess::TryGetMutableNativeComponentMarkModified(*world_, entity, componentId_); data != nullptr) {
            auto* current = static_cast<TransformComponent*>(data);
            *current = bumpVersions(current);
            return;
        }
    }
    // A component that already lives in the native storage is overwritten in place and flagged: no
    // structural bookkeeping.
    if (void* data = kb::ecs::WorldInternalAccess::TryGetMutableNativeComponent(*world_, entity, componentId_); data != nullptr) {
        auto* current = static_cast<TransformComponent*>(data);
        *current = bumpVersions(current);
        kb::ecs::WorldInternalAccess::MarkNativeComponentWritten(*world_, entity, componentId_, sizeof(TransformComponent), current);
        return;
    }
    const TransformComponent stored = bumpVersions(TryGet(entity));
    SceneComponentStorageAccess::Set<TransformComponent>(world_, entity, stored);
}

void SceneTransformComponentStore::MarkModified(SceneEntity entity) noexcept {
    if (TransformComponent* transform = TryGet(entity); transform != nullptr) {
        ++transform->localVersion;
        transform->worldDirty = true;
    }
    if (entity.IsValid()) kb::ecs::WorldInternalAccess::MarkComponentModified(*world_, entity, componentId_);
}

void SceneTransformComponentStore::MarkWritten(SceneEntity entity) noexcept {
    if (const void* data = kb::ecs::WorldInternalAccess::TryGetMutableNativeComponent(*world_, entity, componentId_); data != nullptr) {
        kb::ecs::WorldInternalAccess::MarkNativeComponentWritten(*world_, entity, componentId_, sizeof(TransformComponent), data);
    } else if (entity.IsValid()) {
        kb::ecs::WorldInternalAccess::MarkComponentModified(*world_, entity, componentId_);
    }
}

void SceneTransformComponentStore::MarkParentModified(SceneEntity entity) noexcept {
    if (TransformComponent* transform = TryGet(entity); transform != nullptr) {
        transform->worldDirty = true;
    }
    if (entity.IsValid()) kb::ecs::WorldInternalAccess::MarkComponentModified(*world_, entity, componentId_);
}

} // namespace kb::scene
