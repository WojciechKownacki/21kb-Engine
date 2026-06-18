#include "scene/components/SceneComponentAccess.hpp"

#include "ecs/FlecsEntityIds.hpp"

#include <flecs.h>

namespace kb::scene {

const void* SceneComponentAccess::TryGet(const ecs_world_t* world, SceneEntity entity, std::uint64_t componentId) noexcept {
    if (world == nullptr || !entity.IsValid() || componentId == 0) {
        return nullptr;
    }

    const ecs_entity_t flecsEntity = kb::ecs::FlecsEntityId(entity);
    return ecs_is_alive(world, flecsEntity) ? ecs_get_id(world, flecsEntity, componentId) : nullptr;
}

void* SceneComponentAccess::TryGetMutable(ecs_world_t* world, SceneEntity entity, std::uint64_t componentId) noexcept {
    if (world == nullptr || !entity.IsValid() || componentId == 0) {
        return nullptr;
    }

    const ecs_entity_t flecsEntity = kb::ecs::FlecsEntityId(entity);
    return ecs_is_alive(world, flecsEntity) ? ecs_get_mut_id(world, flecsEntity, componentId) : nullptr;
}

bool SceneComponentAccess::Has(const ecs_world_t* world, SceneEntity entity, std::uint64_t componentId) noexcept {
    if (world == nullptr || !entity.IsValid() || componentId == 0) {
        return false;
    }

    const ecs_entity_t flecsEntity = kb::ecs::FlecsEntityId(entity);
    return ecs_is_alive(world, flecsEntity) && ecs_has_id(world, flecsEntity, componentId);
}

void SceneComponentAccess::Set(ecs_world_t* world, SceneEntity entity, std::uint64_t componentId, std::size_t size, const void* value) {
    if (world == nullptr || !entity.IsValid() || componentId == 0) {
        return;
    }
    const ecs_entity_t flecsEntity = kb::ecs::FlecsEntityId(entity);
    if (!ecs_is_alive(world, flecsEntity)) {
        return;
    }
    ecs_set_id(world, kb::ecs::FlecsEntityId(entity), componentId, size, value);
}

void SceneComponentAccess::Remove(ecs_world_t* world, SceneEntity entity, std::uint64_t componentId) noexcept {
    if (world != nullptr && entity.IsValid() && componentId != 0) {
        const ecs_entity_t flecsEntity = kb::ecs::FlecsEntityId(entity);
        if (ecs_is_alive(world, flecsEntity)) {
            ecs_remove_id(world, flecsEntity, componentId);
        }
    }
}

void SceneComponentAccess::MarkModified(ecs_world_t* world, SceneEntity entity, std::uint64_t componentId) noexcept {
    if (world != nullptr && entity.IsValid() && componentId != 0) {
        const ecs_entity_t flecsEntity = kb::ecs::FlecsEntityId(entity);
        if (ecs_is_alive(world, flecsEntity)) {
            ecs_modified_id(world, flecsEntity, componentId);
        }
    }
}

} // namespace kb::scene
