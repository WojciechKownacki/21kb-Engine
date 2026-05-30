#include "ecs/world/WorldComponentIterator.hpp"

#include "ecs/component/ComponentStorageIteration.hpp"
#include "ecs/component/ComponentStoragePairIteration.hpp"

namespace kb::ecs {

void WorldComponentIterator::ForEach(
    ecs_world_t* world,
    ComponentId componentId,
    std::size_t componentSize,
    RawConstComponentVisitor visitor,
    void* context) {
    ComponentStorageIteration::ForEach(world, componentId, componentSize, visitor, context);
}

void WorldComponentIterator::ForEachMutable(
    ecs_world_t* world,
    ComponentId componentId,
    std::size_t componentSize,
    RawMutableComponentVisitor visitor,
    void* context) {
    ComponentStorageIteration::ForEachMutable(world, componentId, componentSize, visitor, context);
}

void WorldComponentIterator::ForEachPair(
    ecs_world_t* world,
    ComponentId firstComponentId,
    std::size_t firstComponentSize,
    ComponentId secondComponentId,
    std::size_t secondComponentSize,
    RawConstPairVisitor visitor,
    void* context) {
    ComponentStoragePairIteration::ForEachPair(world, firstComponentId, firstComponentSize, secondComponentId, secondComponentSize, visitor, context);
}

} // namespace kb::ecs
