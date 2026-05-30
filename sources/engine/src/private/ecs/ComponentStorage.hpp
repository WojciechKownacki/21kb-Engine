#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/Entity.hpp"

#include <cstddef>

struct ecs_world_t;

namespace kb::ecs {

class ComponentStorage {
public:
    using RawConstComponentVisitor = void (*)(Entity entity, const void* component, void* context);
    using RawMutableComponentVisitor = void (*)(Entity entity, void* component, void* context);
    using RawConstComponentsVisitor = void (*)(Entity entity, const void* first, const void* second, void* context);

    static void Set(ecs_world_t* world, Entity entity, ComponentId componentId, std::size_t size, const void* component);
    [[nodiscard]] static bool Has(const ecs_world_t* world, Entity entity, ComponentId componentId) noexcept;
    [[nodiscard]] static const void* TryGet(const ecs_world_t* world, Entity entity, ComponentId componentId) noexcept;
    [[nodiscard]] static void* TryGetMutable(ecs_world_t* world, Entity entity, ComponentId componentId) noexcept;
    static void Remove(ecs_world_t* world, Entity entity, ComponentId componentId) noexcept;
    static void MarkModified(ecs_world_t* world, Entity entity, ComponentId componentId) noexcept;
    static void ForEach(ecs_world_t* world, ComponentId componentId, std::size_t componentSize, RawConstComponentVisitor visitor, void* context);
    static void ForEachMutable(ecs_world_t* world, ComponentId componentId, std::size_t componentSize, RawMutableComponentVisitor visitor, void* context);
    static void ForEachPair(
        ecs_world_t* world,
        ComponentId firstComponentId,
        std::size_t firstComponentSize,
        ComponentId secondComponentId,
        std::size_t secondComponentSize,
        RawConstComponentsVisitor visitor,
        void* context);
};

} // namespace kb::ecs
