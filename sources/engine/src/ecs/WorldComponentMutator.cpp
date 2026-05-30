#include "ecs/world/WorldComponentMutator.hpp"

#include "ecs/component/ComponentStorageMutation.hpp"

namespace kb::ecs {

void WorldComponentMutator::Set(ecs_world_t* world, Entity entity, ComponentId componentId, std::size_t size, const void* component) {
    ComponentStorageMutation::Set(world, entity, componentId, size, component);
}

void WorldComponentMutator::Remove(ecs_world_t* world, Entity entity, ComponentId componentId) noexcept {
    ComponentStorageMutation::Remove(world, entity, componentId);
}

void WorldComponentMutator::MarkModified(ecs_world_t* world, Entity entity, ComponentId componentId) noexcept {
    ComponentStorageMutation::MarkModified(world, entity, componentId);
}

} // namespace kb::ecs
