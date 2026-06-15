#include "ecs/query/QueryPlan.hpp"

#include "ecs/query/QueryDescriptorBuilder.hpp"

#include <flecs.h>

namespace kb::ecs {

QueryPlan::QueryPlan(
    ecs_world_t* world,
    std::span<const ComponentId> componentIds,
    std::span<const std::size_t> componentSizes,
    std::span<const ComponentId> requiredComponentIds,
    std::span<const ComponentId> optionalComponentIds,
    std::span<const ComponentId> excludedComponentIds,
    std::span<const ComponentId> changedComponentIds)
    : query_(QueryDescriptorBuilder::Build(
          world,
          componentIds,
          componentSizes,
          requiredComponentIds,
          optionalComponentIds,
          excludedComponentIds,
          changedComponentIds))
    , componentIds_(componentIds.begin(), componentIds.end())
    , componentSizes_(componentSizes.begin(), componentSizes.end())
    , hasChangeFilters_(!changedComponentIds.empty()) {}

bool QueryPlan::IsValid() const noexcept {
    return query_ && !componentSizes_.empty();
}

ecs_query_t* QueryPlan::Native() const noexcept {
    return query_.Get();
}

std::span<const ComponentId> QueryPlan::ComponentIds() const noexcept {
    return componentIds_;
}

std::span<const std::size_t> QueryPlan::ComponentSizes() const noexcept {
    return componentSizes_;
}

bool QueryPlan::HasChangeFilters() const noexcept {
    return hasChangeFilters_;
}

bool QueryPlan::MatchesArchetype(ecs_table_t* archetype) const noexcept {
    if (!IsValid() || archetype == nullptr) {
        return false;
    }

    ecs_iter_t iterator{};
    const bool matches = ecs_query_has_table(query_.Get(), archetype, &iterator);
    if (matches) {
        ecs_iter_fini(&iterator);
    }
    return matches;
}

} // namespace kb::ecs
