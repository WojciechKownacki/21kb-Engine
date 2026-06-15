#include "ecs/query/QueryDescriptorBuilder.hpp"

#include "ecs/query/QueryLimits.hpp"

#include <flecs.h>

#include <algorithm>
#include <cstdint>

namespace kb::ecs {
namespace {

[[nodiscard]] bool Contains(std::span<const ComponentId> componentIds, ComponentId componentId) noexcept {
    return std::find(componentIds.begin(), componentIds.end(), componentId) != componentIds.end();
}

[[nodiscard]] bool IsChangeFilterTarget(
    ComponentId componentId,
    std::span<const ComponentId> componentIds,
    std::span<const ComponentId> requiredComponentIds) noexcept {
    return Contains(componentIds, componentId) || Contains(requiredComponentIds, componentId);
}

[[nodiscard]] std::int16_t ResolveInOut(ComponentId componentId, std::span<const ComponentId> changedComponentIds) noexcept {
    if (changedComponentIds.empty()) {
        return static_cast<std::int16_t>(EcsInOutDefault);
    }
    return static_cast<std::int16_t>(Contains(changedComponentIds, componentId) ? EcsIn : EcsInOutFilter);
}

} // namespace

FlecsQueryHandle QueryDescriptorBuilder::Build(
    ecs_world_t* world,
    std::span<const ComponentId> componentIds,
    std::span<const std::size_t> componentSizes,
    std::span<const ComponentId> requiredComponentIds,
    std::span<const ComponentId> optionalComponentIds,
    std::span<const ComponentId> excludedComponentIds,
    std::span<const ComponentId> changedComponentIds) {
    const std::size_t termCount = componentIds.size() + requiredComponentIds.size() + optionalComponentIds.size() + excludedComponentIds.size();
    if (world == nullptr || componentIds.empty() || componentIds.size() != componentSizes.size() || termCount > kMaxQueryTerms) {
        return FlecsQueryHandle{};
    }
    for (ComponentId componentId : changedComponentIds) {
        const bool validTarget = IsChangeFilterTarget(componentId, componentIds, requiredComponentIds);
        const bool conflictingTarget = Contains(optionalComponentIds, componentId) || Contains(excludedComponentIds, componentId);
        if (componentId == 0 || !validTarget || conflictingTarget) {
            return FlecsQueryHandle{};
        }
    }

    ecs_query_desc_t desc{};
    if (!changedComponentIds.empty()) {
        desc.flags = EcsQueryDetectChanges;
    }
    std::size_t termIndex = 0;
    for (std::size_t index = 0; index < componentIds.size(); ++index) {
        if (componentIds[index] == 0 || componentSizes[index] == 0) {
            return FlecsQueryHandle{};
        }
        desc.terms[termIndex].id = componentIds[index];
        desc.terms[termIndex].oper = EcsAnd;
        desc.terms[termIndex].inout = ResolveInOut(componentIds[index], changedComponentIds);
        ++termIndex;
    }

    for (ComponentId componentId : requiredComponentIds) {
        if (componentId == 0) {
            return FlecsQueryHandle{};
        }
        desc.terms[termIndex].id = componentId;
        desc.terms[termIndex].oper = EcsAnd;
        desc.terms[termIndex].inout = ResolveInOut(componentId, changedComponentIds);
        ++termIndex;
    }

    for (ComponentId componentId : optionalComponentIds) {
        if (componentId == 0) {
            return FlecsQueryHandle{};
        }
        desc.terms[termIndex].id = componentId;
        desc.terms[termIndex].oper = EcsOptional;
        desc.terms[termIndex].inout = static_cast<std::int16_t>(changedComponentIds.empty() ? EcsInOutDefault : EcsInOutFilter);
        ++termIndex;
    }

    for (ComponentId componentId : excludedComponentIds) {
        if (componentId == 0) {
            return FlecsQueryHandle{};
        }
        desc.terms[termIndex].id = componentId;
        desc.terms[termIndex].oper = EcsNot;
        desc.terms[termIndex].inout = static_cast<std::int16_t>(changedComponentIds.empty() ? EcsInOutDefault : EcsInOutFilter);
        ++termIndex;
    }

    return FlecsQueryHandle{ ecs_query_init(world, &desc) };
}

FlecsQueryHandle QueryDescriptorBuilder::Build(
    ecs_world_t* world,
    std::span<const ComponentId> componentIds,
    std::span<const std::size_t> componentSizes) {
    return Build(world, componentIds, componentSizes, {}, {}, {}, {});
}

} // namespace kb::ecs
