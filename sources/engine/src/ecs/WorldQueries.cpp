#include "engine/ecs/World.hpp"

#include "ecs/FlecsEntityIds.hpp"
#include "ecs/QueryState.hpp"
#include "ecs/query/QueryPlan.hpp"

#include <flecs.h>

#include <algorithm>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace kb::ecs {
namespace {

template <typename T>
[[nodiscard]] bool SpanEquals(std::span<const T> left, const std::vector<T>& right) noexcept {
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin());
}

[[nodiscard]] std::vector<ComponentId> CopyComponentIds(std::span<const ComponentId> componentIds) {
    return std::vector<ComponentId>{ componentIds.begin(), componentIds.end() };
}

[[nodiscard]] std::vector<std::size_t> CopyComponentSizes(std::span<const std::size_t> componentSizes) {
    return std::vector<std::size_t>{ componentSizes.begin(), componentSizes.end() };
}

} // namespace

QueryState* World::CreateQueryState(
    const ComponentId* componentIds,
    const std::size_t* componentSizes,
    std::size_t componentCount,
    std::span<const ComponentId> requiredComponentIds,
        std::span<const ComponentId> optionalComponentIds,
        std::span<const ComponentId> excludedComponentIds,
        std::span<const ComponentId> changedComponentIds) const {
    if (world_ == nullptr || nativeStorage_ == nullptr || componentIds == nullptr || componentSizes == nullptr || componentCount == 0) {
        return nullptr;
    }

    ++telemetryCounters_.queryPlanRequests;
    const std::span<const ComponentId> selectedComponentIds{ componentIds, componentCount };
    const std::span<const std::size_t> selectedComponentSizes{ componentSizes, componentCount };
    if (std::shared_ptr<QueryPlan> cachedPlan = FindCachedQueryPlan(
            selectedComponentIds,
            selectedComponentSizes,
            requiredComponentIds,
            optionalComponentIds,
            excludedComponentIds,
            changedComponentIds)) {
        ++telemetryCounters_.queryCacheHits;
        return new QueryState{
            nativeStorage_.get(),
            std::move(cachedPlan),
            config_.executionGrainSize,
            mutableComponentBorrowLocks_.get(),
            structuralChangeValidator_.get(),
            &telemetryCounters_,
        };
    }

    ++telemetryCounters_.queryCacheMisses;
    auto plan = std::make_shared<QueryPlan>(
        selectedComponentIds,
        selectedComponentSizes,
        requiredComponentIds,
        optionalComponentIds,
        excludedComponentIds,
        changedComponentIds);
    if (!plan->IsValid()) {
        return nullptr;
    }

    StoreCachedQueryPlan(
        selectedComponentIds,
        selectedComponentSizes,
        requiredComponentIds,
        optionalComponentIds,
        excludedComponentIds,
        changedComponentIds,
        plan);
    return new QueryState{
        nativeStorage_.get(),
        std::move(plan),
        config_.executionGrainSize,
        mutableComponentBorrowLocks_.get(),
        structuralChangeValidator_.get(),
        &telemetryCounters_,
    };
}

std::shared_ptr<QueryPlan> World::FindCachedQueryPlan(
    std::span<const ComponentId> componentIds,
    std::span<const std::size_t> componentSizes,
    std::span<const ComponentId> requiredComponentIds,
    std::span<const ComponentId> optionalComponentIds,
    std::span<const ComponentId> excludedComponentIds,
    std::span<const ComponentId> changedComponentIds) const {
    for (const QueryPlanCacheEntry& entry : queryPlanCache_) {
        if (SpanEquals(componentIds, entry.componentIds)
            && SpanEquals(componentSizes, entry.componentSizes)
            && SpanEquals(requiredComponentIds, entry.requiredComponentIds)
            && SpanEquals(optionalComponentIds, entry.optionalComponentIds)
            && SpanEquals(excludedComponentIds, entry.excludedComponentIds)
            && SpanEquals(changedComponentIds, entry.changedComponentIds)) {
            return entry.plan;
        }
    }
    return {};
}

void World::StoreCachedQueryPlan(
    std::span<const ComponentId> componentIds,
    std::span<const std::size_t> componentSizes,
    std::span<const ComponentId> requiredComponentIds,
    std::span<const ComponentId> optionalComponentIds,
    std::span<const ComponentId> excludedComponentIds,
    std::span<const ComponentId> changedComponentIds,
    std::shared_ptr<QueryPlan> plan) const {
    if (!plan) {
        return;
    }
    queryPlanCache_.push_back(QueryPlanCacheEntry{
        .componentIds = CopyComponentIds(componentIds),
        .componentSizes = CopyComponentSizes(componentSizes),
        .requiredComponentIds = CopyComponentIds(requiredComponentIds),
        .optionalComponentIds = CopyComponentIds(optionalComponentIds),
        .excludedComponentIds = CopyComponentIds(excludedComponentIds),
        .changedComponentIds = CopyComponentIds(changedComponentIds),
        .plan = std::move(plan),
    });
}

ecs_table_t* World::EntityArchetype(Entity entity) const noexcept {
    if (world_ == nullptr || !entity.IsValid() || !ecs_is_alive(world_, FlecsEntityId(entity))) {
        return nullptr;
    }
    return ecs_get_table(world_, FlecsEntityId(entity));
}

void World::InvalidateQueryPlansForArchetypeChange(ecs_table_t* previousArchetype, ecs_table_t* currentArchetype) noexcept {
    if (previousArchetype != currentArchetype || (previousArchetype == nullptr && currentArchetype == nullptr)) {
        RecordStructuralChange();
    }
}

void World::ValidateStructuralChangeAllowed(std::string_view operation) const {
    if (structuralChangeValidator_ != nullptr) {
        structuralChangeValidator_->ValidateStructuralChange(operation);
    }
}

void World::ValidateEntityHandle(Entity entity, std::string_view operation) const {
    if (!entity.IsValid()) {
        throw std::invalid_argument("ECS " + std::string{ operation } + " received an invalid entity handle");
    }
    if (!IsAlive(entity)) {
        throw std::out_of_range("ECS " + std::string{ operation } + " received a stale entity handle");
    }
}

void World::ValidateOptionalEntityHandle(Entity entity, std::string_view operation) const {
    if (entity.IsValid()) {
        ValidateEntityHandle(entity, operation);
    }
}

StructuralChangeValidator::Guard World::EnterIteration() const noexcept {
    return structuralChangeValidator_ != nullptr ? structuralChangeValidator_->EnterIteration() : StructuralChangeValidator::Guard{};
}

void World::RecordStructuralChange(std::size_t count) const noexcept {
    telemetryCounters_.structuralChangesSinceReset += count;
    telemetryCounters_.totalStructuralChanges += count;
}

} // namespace kb::ecs
