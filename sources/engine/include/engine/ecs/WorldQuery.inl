#pragma once

#include "engine/ecs/Query.hpp"

#include <array>
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace kb::ecs {

template <typename... Components>
Query<Components...> World::CreateQuery() {
    return CreateQuery<Components...>(QueryFilter{});
}

template <typename... Components>
Query<Components...> World::CreateQuery(const QueryFilter& filter) {
    static_assert(sizeof...(Components) > 0, "ECS query must have at least one component");
    (ValidateComponentType<Components>(), ...);

    std::array<ComponentId, sizeof...(Components)> componentIds{ RegisterComponent<Components>()... };
    std::array<std::size_t, sizeof...(Components)> componentSizes{ sizeof(Components)... };
    std::vector<ComponentId> requiredComponentIds;
    requiredComponentIds.reserve(filter.RequiredComponents().size());
    for (ComponentId requiredComponentId : filter.RequiredComponents()) {
        if (std::find(componentIds.begin(), componentIds.end(), requiredComponentId) == componentIds.end()) {
            requiredComponentIds.push_back(requiredComponentId);
        }
    }
    for (ComponentId optionalComponentId : filter.OptionalComponents()) {
        if (std::find(componentIds.begin(), componentIds.end(), optionalComponentId) != componentIds.end()) {
            throw std::invalid_argument("ECS query filter cannot make a selected query component optional");
        }
    }
    for (ComponentId excludedComponentId : filter.ExcludedComponents()) {
        if (std::find(componentIds.begin(), componentIds.end(), excludedComponentId) != componentIds.end()) {
            throw std::invalid_argument("ECS query filter cannot exclude a selected query component");
        }
    }
    return Query<Components...>{ CreateQueryState(
        componentIds.data(),
        componentSizes.data(),
        componentIds.size(),
        requiredComponentIds,
        filter.OptionalComponents(),
        filter.ExcludedComponents(),
        filter.ChangedComponents()) };
}

} // namespace kb::ecs
