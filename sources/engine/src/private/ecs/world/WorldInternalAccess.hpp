#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/Entity.hpp"

#include "engine/ecs/ComponentReflection.hpp"

#include <cstddef>
#include <span>
#include <string_view>
#include <typeindex>

struct ecs_world_t;

namespace kb::ecs {

class World;
class WorldRegistrySet;

class WorldInternalAccess {
public:
    [[nodiscard]] static ecs_world_t* Native(World& world) noexcept;
    [[nodiscard]] static const ecs_world_t* Native(const World& world) noexcept;
    [[nodiscard]] static WorldRegistrySet* Registries(World& world) noexcept;
    [[nodiscard]] static const WorldRegistrySet* Registries(const World& world) noexcept;
    [[nodiscard]] static Entity ResolveAliveEntity(const World& world, Entity::IdType entityIdWithoutGeneration) noexcept;

    [[nodiscard]] static const void* TryGetComponent(const World& world, Entity entity, ComponentId componentId);
    [[nodiscard]] static void* TryGetMutableComponent(World& world, Entity entity, ComponentId componentId);
    // Native optional lookup validates lifetime; no backend lookup or copied state.
    [[nodiscard]] static void* TryGetMutableNativeComponent(World& world, Entity entity, ComponentId componentId);
    // Same lookup, and the row is flagged modified in the same pass.
    [[nodiscard]] static void* TryGetMutableNativeComponentMarkModified(World& world, Entity entity, ComponentId componentId);
    // MarkComponentModified for a native component the caller just wrote through `data` (size known):
    // the same mirroring and OnSet behaviour with fewer lookups.
    static void MarkNativeComponentWritten(World& world, Entity entity, ComponentId componentId, std::size_t size, const void* data);
    static void MarkComponentModified(World& world, Entity entity, ComponentId componentId);
    static void SetComponent(World& world, Entity entity, ComponentId componentId, std::size_t size, const void* component);
    // Registers a component type known only by its type_info, size and alignment, and describes its fields.
    [[nodiscard]] static ComponentId RegisterComponent(World& world, std::type_index type, std::string_view name, std::size_t size, std::size_t alignment);
    [[nodiscard]] static const ComponentReflection* RegisterComponentReflection(World& world, ComponentId componentId, std::string_view name, std::size_t size, std::span<const ComponentFieldDesc> fields);
    [[nodiscard]] static bool HasComponent(const World& world, Entity entity, ComponentId componentId);
    static void RemoveComponent(World& world, Entity entity, ComponentId componentId);
    // Visits every entity holding the component; the visitor must not change the world.
    static void ForEachComponent(const World& world, ComponentId componentId, std::size_t size, void (*visitor)(Entity, const void*, void*), void* context);
};

} // namespace kb::ecs
