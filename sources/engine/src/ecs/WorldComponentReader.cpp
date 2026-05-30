#include "ecs/world/WorldComponentReader.hpp"

#include "ecs/component/ComponentStorageQuery.hpp"

namespace kb::ecs {

bool WorldComponentReader::Has(ecs_world_t* world, Entity entity, ComponentId componentId) noexcept {
    return ComponentStorageQuery::Has(world, entity, componentId);
}

const void* WorldComponentReader::TryGet(ecs_world_t* world, Entity entity, ComponentId componentId) noexcept {
    return ComponentStorageQuery::TryGet(world, entity, componentId);
}

void* WorldComponentReader::TryGetMutable(ecs_world_t* world, Entity entity, ComponentId componentId) noexcept {
    return ComponentStorageQuery::TryGetMutable(world, entity, componentId);
}

} // namespace kb::ecs
