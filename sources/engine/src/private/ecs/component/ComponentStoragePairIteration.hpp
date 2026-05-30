#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/Entity.hpp"

#include <cstddef>

struct ecs_world_t;

namespace kb::ecs {

class ComponentStoragePairIteration {
public:
    using RawConstComponentsVisitor = void (*)(Entity entity, const void* first, const void* second, void* context);

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
