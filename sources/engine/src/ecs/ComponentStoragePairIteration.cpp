#include "ecs/component/ComponentStoragePairIteration.hpp"

#include "ecs/query/QueryDescriptorBuilder.hpp"

#include <flecs.h>

#include <array>
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

    const std::array<ComponentId, 2> componentIds{ firstComponentId, secondComponentId };
    const std::array<std::size_t, 2> componentSizes{ firstComponentSize, secondComponentSize };
    FlecsQueryHandle query = QueryDescriptorBuilder::Build(world, componentIds, componentSizes);
    if (!query) {
        return;
    }

    ecs_iter_t it = ecs_query_iter(world, query.Get());
    while (ecs_query_next(&it)) {
        const void* firstComponents = ecs_field_w_size(&it, static_cast<ecs_size_t>(firstComponentSize), 0);
        const void* secondComponents = ecs_field_w_size(&it, static_cast<ecs_size_t>(secondComponentSize), 1);
        if (firstComponents == nullptr || secondComponents == nullptr) {
            continue;
        }

        const auto* firstBytes = static_cast<const std::uint8_t*>(firstComponents);
        const auto* secondBytes = static_cast<const std::uint8_t*>(secondComponents);
        for (int32_t i = 0; i < it.count; ++i) {
            visitor(
                Entity{ it.entities[i] },
                firstBytes + static_cast<std::size_t>(i) * firstComponentSize,
                secondBytes + static_cast<std::size_t>(i) * secondComponentSize,
                context);
        }
    }
}

} // namespace kb::ecs
