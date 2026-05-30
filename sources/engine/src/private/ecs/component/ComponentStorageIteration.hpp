#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/Entity.hpp"

#include <cstddef>

struct ecs_world_t;

namespace kb::ecs {

class ComponentStorageIteration {
public:
    using RawConstComponentVisitor = void (*)(Entity entity, const void* component, void* context);
    using RawMutableComponentVisitor = void (*)(Entity entity, void* component, void* context);

    static void ForEach(ecs_world_t* world, ComponentId componentId, std::size_t componentSize, RawConstComponentVisitor visitor, void* context);
    static void ForEachMutable(ecs_world_t* world, ComponentId componentId, std::size_t componentSize, RawMutableComponentVisitor visitor, void* context);
};

} // namespace kb::ecs
