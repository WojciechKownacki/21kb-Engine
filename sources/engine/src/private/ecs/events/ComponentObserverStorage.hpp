#pragma once

#include "ecs/events/ComponentObserverTypes.hpp"
#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/TypeIds.hpp"

#include <cstddef>

struct ecs_world_t;

namespace kb::ecs {

class ComponentObserverStorage {
public:
    [[nodiscard]] static ObserverId Create(
        ecs_world_t* world,
        ComponentId componentId,
        std::size_t componentSize,
        ComponentEventKind event,
        RawComponentObserverVisitor visitor,
        void* context,
        ComponentObserverContextFree contextFree,
        bool yieldExisting) noexcept;

    static void Destroy(ecs_world_t* world, ObserverId observer) noexcept;
};

} // namespace kb::ecs
