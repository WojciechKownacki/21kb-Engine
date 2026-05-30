#include "engine/ecs/World.hpp"

#include "ecs/world/WorldComponentIterator.hpp"
#include "ecs/world/WorldComponentMutator.hpp"
#include "ecs/world/WorldComponentReader.hpp"
#include "ecs/world/WorldComponentRegistrar.hpp"

namespace kb::ecs {

ComponentId World::RegisterComponent(std::type_index type, std::string_view name, std::size_t size, std::size_t alignment) {
    return WorldComponentRegistrar::Register(world_, registries_.get(), type, name, size, alignment);
}

ComponentId World::FindComponent(std::type_index type) const noexcept {
    return WorldComponentRegistrar::Find(registries_.get(), type);
}

void World::SetComponent(Entity entity, ComponentId componentId, std::size_t size, const void* component) {
    WorldComponentMutator::Set(world_, entity, componentId, size, component);
}

bool World::HasComponent(Entity entity, ComponentId componentId) const noexcept {
    return WorldComponentReader::Has(world_, entity, componentId);
}

const void* World::TryGetComponent(Entity entity, ComponentId componentId) const noexcept {
    return WorldComponentReader::TryGet(world_, entity, componentId);
}

void* World::TryGetMutableComponent(Entity entity, ComponentId componentId) noexcept {
    return WorldComponentReader::TryGetMutable(world_, entity, componentId);
}

void World::RemoveComponent(Entity entity, ComponentId componentId) noexcept {
    WorldComponentMutator::Remove(world_, entity, componentId);
}

void World::MarkComponentModified(Entity entity, ComponentId componentId) noexcept {
    WorldComponentMutator::MarkModified(world_, entity, componentId);
}

void World::ForEachComponent(ComponentId componentId, std::size_t componentSize, RawConstComponentVisitor visitor, void* context) const {
    WorldComponentIterator::ForEach(world_, componentId, componentSize, visitor, context);
}

void World::ForEachMutableComponent(ComponentId componentId, std::size_t componentSize, RawMutableComponentVisitor visitor, void* context) {
    WorldComponentIterator::ForEachMutable(world_, componentId, componentSize, visitor, context);
}

void World::ForEachComponents(
    ComponentId firstComponentId,
    std::size_t firstComponentSize,
    ComponentId secondComponentId,
    std::size_t secondComponentSize,
    void (*visitor)(Entity entity, const void* first, const void* second, void* context),
    void* context) const {
    WorldComponentIterator::ForEachPair(world_, firstComponentId, firstComponentSize, secondComponentId, secondComponentSize, visitor, context);
}

} // namespace kb::ecs
