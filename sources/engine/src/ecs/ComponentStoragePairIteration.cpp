#include "ecs/component/ComponentStoragePairIteration.hpp"

#include <flecs.h>

#include <cstdint>

namespace kb::ecs {

void ComponentStoragePairIteration::ForEachPair(
    ecs_world_t* world,
    ComponentId firstComponentId,
    std::size_t firstComponentSize,
    ComponentId secondComponentId,
    std::size_t secondComponentSize,
    RawConstComponentsVisitor visitor,
    void* context) {
    if (world == nullptr || firstComponentId == 0 || secondComponentId == 0 || firstComponentSize == 0 || secondComponentSize == 0 || visitor == nullptr) {
        return;
    }

    ecs_iter_t it = ecs_each_id(world, firstComponentId);
    while (ecs_each_next(&it)) {
        const void* firstComponents = ecs_field_w_size(&it, static_cast<ecs_size_t>(firstComponentSize), 0);
        if (firstComponents == nullptr) {
            continue;
        }

        const auto* firstBytes = static_cast<const std::uint8_t*>(firstComponents);
        for (int32_t i = 0; i < it.count; ++i) {
            const void* secondComponent = ecs_get_id(world, it.entities[i], secondComponentId);
            if (secondComponent == nullptr) {
                continue;
            }

            visitor(
                Entity{ it.entities[i] },
                firstBytes + static_cast<std::size_t>(i) * firstComponentSize,
                secondComponent,
                context);
        }
    }
}

} // namespace kb::ecs
