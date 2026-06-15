#include "ecs/query/QueryPlan.hpp"

#include "ecs/query/QueryLimits.hpp"

#include <algorithm>

namespace kb::ecs {
namespace {

[[nodiscard]] bool Contains(std::span<const ComponentId> componentIds, ComponentId componentId) noexcept {
    return std::find(componentIds.begin(), componentIds.end(), componentId) != componentIds.end();
}

[[nodiscard]] bool ContainsDuplicateOrZero(std::span<const ComponentId> componentIds) noexcept {
    for (std::size_t index = 0; index < componentIds.size(); ++index) {
        if (componentIds[index] == 0) {
            return true;
        }
        for (std::size_t compare = index + 1; compare < componentIds.size(); ++compare) {
            if (componentIds[index] == componentIds[compare]) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] bool ContainsZeroSize(std::span<const std::size_t> componentSizes) noexcept {
    return std::find(componentSizes.begin(), componentSizes.end(), 0U) != componentSizes.end();
}

[[nodiscard]] bool ValidateChangedFilters(
    std::span<const ComponentId> componentIds,
    std::span<const ComponentId> requiredComponentIds,
    std::span<const ComponentId> optionalComponentIds,
    std::span<const ComponentId> excludedComponentIds,
    std::span<const ComponentId> changedComponentIds) noexcept {
    for (ComponentId componentId : changedComponentIds) {
        if (!Contains(componentIds, componentId) && !Contains(requiredComponentIds, componentId)) {
            return false;
        }
        if (Contains(optionalComponentIds, componentId) || Contains(excludedComponentIds, componentId)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ValidatePlan(
    std::span<const ComponentId> componentIds,
    std::span<const std::size_t> componentSizes,
    std::span<const ComponentId> requiredComponentIds,
    std::span<const ComponentId> optionalComponentIds,
    std::span<const ComponentId> excludedComponentIds,
    std::span<const ComponentId> changedComponentIds) noexcept {
    const std::size_t termCount =
        componentIds.size() + requiredComponentIds.size() + optionalComponentIds.size() + excludedComponentIds.size();
    if (componentIds.empty() || componentIds.size() != componentSizes.size() || termCount > kMaxQueryTerms) {
        return false;
    }
    if (ContainsDuplicateOrZero(componentIds) || ContainsDuplicateOrZero(requiredComponentIds) || ContainsDuplicateOrZero(optionalComponentIds)
        || ContainsDuplicateOrZero(excludedComponentIds) || ContainsDuplicateOrZero(changedComponentIds) || ContainsZeroSize(componentSizes)) {
        return false;
    }
    for (ComponentId componentId : optionalComponentIds) {
        if (Contains(componentIds, componentId) || Contains(requiredComponentIds, componentId) || Contains(excludedComponentIds, componentId)) {
            return false;
        }
    }
    for (ComponentId componentId : requiredComponentIds) {
        if (Contains(optionalComponentIds, componentId) || Contains(excludedComponentIds, componentId)) {
            return false;
        }
    }
    for (ComponentId componentId : excludedComponentIds) {
        if (Contains(componentIds, componentId) || Contains(requiredComponentIds, componentId) || Contains(optionalComponentIds, componentId)) {
            return false;
        }
    }
    return ValidateChangedFilters(componentIds, requiredComponentIds, optionalComponentIds, excludedComponentIds, changedComponentIds);
}

} // namespace

QueryPlan::QueryPlan(
    std::span<const ComponentId> componentIds,
    std::span<const std::size_t> componentSizes,
    std::span<const ComponentId> requiredComponentIds,
    std::span<const ComponentId> optionalComponentIds,
    std::span<const ComponentId> excludedComponentIds,
    std::span<const ComponentId> changedComponentIds)
    : componentIds_(componentIds.begin(), componentIds.end())
    , componentSizes_(componentSizes.begin(), componentSizes.end())
    , requiredComponentIds_(requiredComponentIds.begin(), requiredComponentIds.end())
    , optionalComponentIds_(optionalComponentIds.begin(), optionalComponentIds.end())
    , excludedComponentIds_(excludedComponentIds.begin(), excludedComponentIds.end())
    , changedComponentIds_(changedComponentIds.begin(), changedComponentIds.end())
    , hasChangeFilters_(!changedComponentIds.empty())
    , valid_(ValidatePlan(
          componentIds_,
          componentSizes_,
          requiredComponentIds_,
          optionalComponentIds_,
          excludedComponentIds_,
          changedComponentIds_)) {}

bool QueryPlan::IsValid() const noexcept {
    return valid_;
}

std::span<const ComponentId> QueryPlan::ComponentIds() const noexcept {
    return componentIds_;
}

std::span<const std::size_t> QueryPlan::ComponentSizes() const noexcept {
    return componentSizes_;
}

std::span<const ComponentId> QueryPlan::RequiredComponentIds() const noexcept {
    return requiredComponentIds_;
}

std::span<const ComponentId> QueryPlan::OptionalComponentIds() const noexcept {
    return optionalComponentIds_;
}

std::span<const ComponentId> QueryPlan::ExcludedComponentIds() const noexcept {
    return excludedComponentIds_;
}

std::span<const ComponentId> QueryPlan::ChangedComponentIds() const noexcept {
    return changedComponentIds_;
}

bool QueryPlan::HasChangeFilters() const noexcept {
    return hasChangeFilters_;
}

} // namespace kb::ecs
