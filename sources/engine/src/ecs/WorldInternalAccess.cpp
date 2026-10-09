#include "ecs/world/WorldInternalAccess.hpp"

#include "engine/ecs/World.hpp"
#include "ecs/world/WorldComponentMutator.hpp"
#include "engine/ecs/NativeArchetypeStorage.hpp"

namespace kb::ecs {

ecs_world_t* WorldInternalAccess::Native(World& world) noexcept {
    return world.world_;
}

const ecs_world_t* WorldInternalAccess::Native(const World& world) noexcept {
    return world.world_;
}

WorldRegistrySet* WorldInternalAccess::Registries(World& world) noexcept {
    return world.registries_.get();
}

const WorldRegistrySet* WorldInternalAccess::Registries(const World& world) noexcept {
    return world.registries_.get();
}

Entity WorldInternalAccess::ResolveAliveEntity(const World& world, Entity::IdType entityIdWithoutGeneration) noexcept {
    return world.ResolveAliveEntity(entityIdWithoutGeneration);
}

const void* WorldInternalAccess::TryGetComponent(const World& world, Entity entity, ComponentId componentId) {
    return world.TryGetComponent(entity, componentId);
}

void* WorldInternalAccess::TryGetMutableComponent(World& world, Entity entity, ComponentId componentId) {
    return world.TryGetMutableComponent(entity, componentId);
}

void* WorldInternalAccess::TryGetMutableNativeComponent(World& world, Entity entity, ComponentId componentId) {
    return world.nativeStorage_ == nullptr ? nullptr : world.nativeStorage_->TryGetMutableComponentData(entity, componentId);
}

void* WorldInternalAccess::TryGetMutableNativeComponentMarkModified(World& world, Entity entity, ComponentId componentId) {
    return world.nativeStorage_ == nullptr ? nullptr : world.nativeStorage_->TryGetMutableComponentDataMarkModified(entity, componentId);
}

void WorldInternalAccess::MarkNativeComponentWritten(World& world, Entity entity, ComponentId componentId, std::size_t size, const void* data) {
    if (world.MirrorsValueWrites(componentId)) {
        WorldComponentMutator::SetExisting(world.world_, entity, componentId, size, data);
    }
    // OnSet may remove this component, migrate its row or destroy the entity; the lookup that flags the row
    // also tells whether it is still there.
    static_cast<void>(world.nativeStorage_->TryGetMutableComponentDataMarkModified(entity, componentId));
}

void WorldInternalAccess::MarkComponentModified(World& world, Entity entity, ComponentId componentId) {
    world.MarkComponentModified(entity, componentId);
}

void WorldInternalAccess::SetComponent(World& world, Entity entity, ComponentId componentId, std::size_t size, const void* component) {
    world.SetComponent(entity, componentId, size, component);
}

} // namespace kb::ecs
