#pragma once

#include "engine/ecs/ComponentId.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace kb::ecs {

class QueryPlan {
public:
    QueryPlan(
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
    [[nodiscard]] std::span<const ComponentId> ComponentIds() const noexcept;
    [[nodiscard]] std::span<const std::size_t> ComponentSizes() const noexcept;
    [[nodiscard]] std::span<const ComponentId> RequiredComponentIds() const noexcept;
    [[nodiscard]] std::span<const ComponentId> OptionalComponentIds() const noexcept;
    [[nodiscard]] std::span<const ComponentId> ExcludedComponentIds() const noexcept;
    [[nodiscard]] std::span<const ComponentId> ChangedComponentIds() const noexcept;
    [[nodiscard]] bool HasChangeFilters() const noexcept;

private:
    std::vector<ComponentId> componentIds_;
    std::vector<std::size_t> componentSizes_;
    std::vector<ComponentId> requiredComponentIds_;
    std::vector<ComponentId> optionalComponentIds_;
    std::vector<ComponentId> excludedComponentIds_;
    std::vector<ComponentId> changedComponentIds_;
    bool hasChangeFilters_ = false;
    bool valid_ = false;
};

} // namespace kb::ecs
