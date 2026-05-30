#include "scene/components/SceneComponentAccess.hpp"

#include <flecs.h>

namespace kb::scene {

const void* SceneComponentAccess::TryGet(const ecs_world_t* world, SceneEntity entity, std::uint64_t componentId) noexcept {
    if (world == nullptr || !entity.IsValid()) {
        return nullptr;
    }

    return ecs_get_id(world, entity.Id(), componentId);
}

void* SceneComponentAccess::TryGetMutable(ecs_world_t* world, SceneEntity entity, std::uint64_t componentId) noexcept {
    if (world == nullptr || !entity.IsValid()) {
        return nullptr;
    }

    return ecs_get_mut_id(world, entity.Id(), componentId);
}

bool SceneComponentAccess::Has(const ecs_world_t* world, SceneEntity entity, std::uint64_t componentId) noexcept {
    return world != nullptr && entity.IsValid() && ecs_has_id(world, entity.Id(), componentId);
}

void SceneComponentAccess::Set(ecs_world_t* world, SceneEntity entity, std::uint64_t componentId, std::size_t size, const void* value) {
    ecs_set_id(world, entity.Id(), componentId, size, value);
}

void SceneComponentAccess::Remove(ecs_world_t* world, SceneEntity entity, std::uint64_t componentId) noexcept {
    if (world != nullptr && entity.IsValid()) {
        ecs_remove_id(world, entity.Id(), componentId);
    }
}

void SceneComponentAccess::MarkModified(ecs_world_t* world, SceneEntity entity, std::uint64_t componentId) noexcept {
    if (world != nullptr && entity.IsValid()) {
        ecs_modified_id(world, entity.Id(), componentId);
    }
}

} // namespace kb::scene
