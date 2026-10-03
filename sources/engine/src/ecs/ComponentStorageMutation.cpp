#include "ecs/component/ComponentStorageMutation.hpp"

#include "ecs/FlecsEntityIds.hpp"
#include "ecs/component/ComponentStorageQuery.hpp"

#include <flecs.h>
#include <cstring>

namespace kb::ecs {

void ComponentStorageMutation::Set(ecs_world_t* world, Entity entity, ComponentId componentId, std::size_t size, const void* component) {
    if (world != nullptr && entity.IsValid() && componentId != 0 && size != 0 && component != nullptr && ecs_is_alive(world, FlecsEntityId(entity))) {
        ecs_set_id(world, FlecsEntityId(entity), componentId, size, component);
    }
}

void ComponentStorageMutation::SetExisting(ecs_world_t* world, Entity entity, ComponentId componentId, std::size_t size, const void* component, bool createIfMissing) {
    if (world == nullptr || !entity.IsValid() || componentId == 0 || size == 0 || component == nullptr || !ecs_is_alive(world, FlecsEntityId(entity))) {
        return;
    }
    if (ecs_is_deferred(world)) {
        if (createIfMissing || ecs_has_id(world, FlecsEntityId(entity), componentId)) {
            ecs_set_id(world, FlecsEntityId(entity), componentId, size, component);
        }
        return;
    }
    // Components registered by World are trivially copyable. Preserve OnSet
    // notifications while avoiding the structural ensure path for each row.
    if (void* destination = ecs_get_mut_id(world, FlecsEntityId(entity), componentId); destination != nullptr) {
        std::memcpy(destination, component, size);
        ecs_modified_id(world, FlecsEntityId(entity), componentId);
    } else if (createIfMissing || ecs_has_id(world, FlecsEntityId(entity), componentId)) {
        // An inherited value needs an owned override; never mutate the base.
        ecs_set_id(world, FlecsEntityId(entity), componentId, size, component);
    }
}

void ComponentStorageMutation::Remove(ecs_world_t* world, Entity entity, ComponentId componentId) noexcept {
    if (ComponentStorageQuery::Has(world, entity, componentId)) {
        ecs_remove_id(world, FlecsEntityId(entity), componentId);
    }
}

void ComponentStorageMutation::MarkModified(ecs_world_t* world, Entity entity, ComponentId componentId) noexcept {
    if (ComponentStorageQuery::Has(world, entity, componentId)) {
        ecs_modified_id(world, FlecsEntityId(entity), componentId);
    }
}

} // namespace kb::ecs
