#include "engine/ecs/QueryFilter.hpp"

#include <algorithm>
#include <stdexcept>

namespace kb::ecs {
namespace {

[[nodiscard]] bool Contains(std::span<const ComponentId> componentIds, ComponentId componentId) noexcept {
    return std::find(componentIds.begin(), componentIds.end(), componentId) != componentIds.end();
}

void ValidateComponentId(ComponentId componentId) {
    if (componentId == 0) {
        throw std::invalid_argument("ECS query filter component id must be valid");
    }
}

} // namespace

QueryFilter& QueryFilter::Require(ComponentId componentId) {
    ValidateComponentId(componentId);
    if (Contains(excluded_, componentId)) {
        throw std::invalid_argument("ECS query filter cannot require and exclude the same component");
    }
    if (!Contains(required_, componentId)) {
        required_.push_back(componentId);
    }
    return *this;
}

QueryFilter& QueryFilter::Optional(ComponentId componentId) {
    ValidateComponentId(componentId);
    if (Contains(excluded_, componentId)) {
        throw std::invalid_argument("ECS query filter cannot make an excluded component optional");
    }
    if (Contains(changed_, componentId)) {
        throw std::invalid_argument("ECS query filter cannot make a change-filtered component optional");
    }
    if (!Contains(required_, componentId) && !Contains(optional_, componentId)) {
        optional_.push_back(componentId);
    }
    return *this;
}

QueryFilter& QueryFilter::Exclude(ComponentId componentId) {
    ValidateComponentId(componentId);
    if (Contains(required_, componentId) || Contains(optional_, componentId) || Contains(changed_, componentId)) {
        throw std::invalid_argument("ECS query filter cannot exclude a required, optional or change-filtered component");
    }
    if (!Contains(excluded_, componentId)) {
        excluded_.push_back(componentId);
    }
    return *this;
}

QueryFilter& QueryFilter::Changed(ComponentId componentId) {
    ValidateComponentId(componentId);
    if (Contains(excluded_, componentId) || Contains(optional_, componentId)) {
        throw std::invalid_argument("ECS query filter cannot change-filter an excluded or optional component");
    }
    if (!Contains(changed_, componentId)) {
        changed_.push_back(componentId);
    }
    if (!Contains(required_, componentId)) {
        required_.push_back(componentId);
    }
    return *this;
}

std::span<const ComponentId> QueryFilter::RequiredComponents() const noexcept {
    return required_;
}

std::span<const ComponentId> QueryFilter::OptionalComponents() const noexcept {
    return optional_;
}

std::span<const ComponentId> QueryFilter::ExcludedComponents() const noexcept {
    return excluded_;
}

std::span<const ComponentId> QueryFilter::ChangedComponents() const noexcept {
    return changed_;
}

} // namespace kb::ecs
