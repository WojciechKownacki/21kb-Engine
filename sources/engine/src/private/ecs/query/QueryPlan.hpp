#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "ecs/query/FlecsQueryHandle.hpp"

#include <cstddef>
#include <span>
#include <vector>

struct ecs_query_t;
struct ecs_table_t;
struct ecs_world_t;

namespace kb::ecs {

class QueryPlan {
public:
    QueryPlan(
        ecs_world_t* world,
        std::span<const ComponentId> componentIds,
        std::span<const std::size_t> componentSizes,
        std::span<const ComponentId> requiredComponentIds,
        std::span<const ComponentId> optionalComponentIds,
        std::span<const ComponentId> excludedComponentIds,
        std::span<const ComponentId> changedComponentIds);

    QueryPlan(const QueryPlan&) = delete;
    QueryPlan& operator=(const QueryPlan&) = delete;
    QueryPlan(QueryPlan&&) = delete;
    QueryPlan& operator=(QueryPlan&&) = delete;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] ecs_query_t* Native() const noexcept;
    [[nodiscard]] std::span<const ComponentId> ComponentIds() const noexcept;
    [[nodiscard]] std::span<const std::size_t> ComponentSizes() const noexcept;
    [[nodiscard]] bool HasChangeFilters() const noexcept;
    [[nodiscard]] bool MatchesArchetype(ecs_table_t* archetype) const noexcept;

private:
    FlecsQueryHandle query_;
    std::vector<ComponentId> componentIds_;
    std::vector<std::size_t> componentSizes_;
    bool hasChangeFilters_ = false;
};

} // namespace kb::ecs
