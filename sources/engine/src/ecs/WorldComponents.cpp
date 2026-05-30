#include "engine/ecs/World.hpp"

#include "ecs/ComponentRegistry.hpp"
#include "ecs/component/ComponentStorageIteration.hpp"
#include "ecs/component/ComponentStorageMutation.hpp"
#include "ecs/component/ComponentStoragePairIteration.hpp"
#include "ecs/component/ComponentStorageQuery.hpp"

namespace kb::ecs {

ComponentId World::RegisterComponent(std::type_index type, std::string_view name, std::size_t size, std::size_t alignment) {
    return components_ == nullptr ? 0 : components_->Register(world_, type, name, size, alignment);
}

ComponentId World::FindComponent(std::type_index type) const noexcept {
    return components_ == nullptr ? 0 : components_->Find(type);
}

void World::SetComponent(Entity entity, ComponentId componentId, std::size_t size, const void* component) {
    ComponentStorageMutation::Set(world_, entity, componentId, size, component);
}

bool World::HasComponent(Entity entity, ComponentId componentId) const noexcept {
    return ComponentStorageQuery::Has(world_, entity, componentId);
}

const void* World::TryGetComponent(Entity entity, ComponentId componentId) const noexcept {
    return ComponentStorageQuery::TryGet(world_, entity, componentId);
}

void* World::TryGetMutableComponent(Entity entity, ComponentId componentId) noexcept {
    return ComponentStorageQuery::TryGetMutable(world_, entity, componentId);
}

void World::RemoveComponent(Entity entity, ComponentId componentId) noexcept {
    ComponentStorageMutation::Remove(world_, entity, componentId);
}

void World::MarkComponentModified(Entity entity, ComponentId componentId) noexcept {
    ComponentStorageMutation::MarkModified(world_, entity, componentId);
}

void World::ForEachComponent(ComponentId componentId, std::size_t componentSize, RawConstComponentVisitor visitor, void* context) const {
    ComponentStorageIteration::ForEach(world_, componentId, componentSize, visitor, context);
}

void World::ForEachMutableComponent(ComponentId componentId, std::size_t componentSize, RawMutableComponentVisitor visitor, void* context) {
    ComponentStorageIteration::ForEachMutable(world_, componentId, componentSize, visitor, context);
}

void World::ForEachComponents(
    ComponentId firstComponentId,
    std::size_t firstComponentSize,
    ComponentId secondComponentId,
    std::size_t secondComponentSize,
    void (*visitor)(Entity entity, const void* first, const void* second, void* context),
    void* context) const {
    ComponentStoragePairIteration::ForEachPair(world_, firstComponentId, firstComponentSize, secondComponentId, secondComponentSize, visitor, context);
}

} // namespace kb::ecs
