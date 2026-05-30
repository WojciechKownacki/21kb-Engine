#pragma once

#include "engine/ecs/Query.hpp"

#include <array>

namespace kb::ecs {

template <typename... Components>
Query<Components...> World::CreateQuery() {
    static_assert(sizeof...(Components) > 0, "ECS query must have at least one component");
    (ValidateComponentType<Components>(), ...);

    std::array<ComponentId, sizeof...(Components)> componentIds{ RegisterComponent<Components>()... };
    std::array<std::size_t, sizeof...(Components)> componentSizes{ sizeof(Components)... };
    return Query<Components...>{ CreateQueryState(componentIds.data(), componentSizes.data(), componentIds.size()) };
}

} // namespace kb::ecs
