#include "ecs/query/QueryDescriptorBuilder.hpp"

#include "ecs/query/QueryLimits.hpp"

#include <flecs.h>

namespace kb::ecs {

FlecsQueryHandle QueryDescriptorBuilder::Build(
    ecs_world_t* world,
    std::span<const ComponentId> componentIds,
    std::span<const std::size_t> componentSizes) {
    if (world == nullptr || componentIds.empty() || componentIds.size() != componentSizes.size() || componentIds.size() > kMaxQueryTerms) {
        return FlecsQueryHandle{};
    }

    ecs_query_desc_t desc{};
    for (std::size_t index = 0; index < componentIds.size(); ++index) {
        if (componentIds[index] == 0 || componentSizes[index] == 0) {
            return FlecsQueryHandle{};
        }
        desc.terms[index].id = componentIds[index];
    }

    return FlecsQueryHandle{ ecs_query_init(world, &desc) };
}

} // namespace kb::ecs
