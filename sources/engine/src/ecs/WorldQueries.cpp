#include "engine/ecs/World.hpp"

#include "ecs/FlecsEntityIds.hpp"
#include "ecs/QueryState.hpp"
#include "ecs/query/QueryPlan.hpp"

#include <flecs.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iterator>
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

template <typename T>
void HashValues(std::size_t& hash, std::span<const T> values) noexcept {
    auto combine = [&hash](std::size_t value) {
        hash ^= value + static_cast<std::size_t>(0x9E3779B97F4A7C15ULL) + (hash << 6U) + (hash >> 2U);
    };
    combine(values.size());
    for (T value : values) {
        combine(std::hash<T>{}(value));
    }
}

[[nodiscard]] std::size_t QueryPlanHash(
    std::span<const ComponentId> componentIds,
    std::span<const std::size_t> componentSizes,
    std::span<const ComponentId> requiredComponentIds,
    std::span<const ComponentId> optionalComponentIds,
    std::span<const ComponentId> excludedComponentIds,
    std::span<const ComponentId> changedComponentIds) noexcept {
    std::size_t hash = 0;
    HashValues(hash, componentIds);
    HashValues(hash, componentSizes);
    HashValues(hash, requiredComponentIds);
    HashValues(hash, optionalComponentIds);
    HashValues(hash, excludedComponentIds);
    HashValues(hash, changedComponentIds);
    return hash;
}

[[nodiscard]] std::uint64_t ElapsedNanoseconds(std::chrono::steady_clock::time_point startedAt) noexcept {
    std::uint64_t elapsedNanoseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - startedAt).count());
    return elapsedNanoseconds == 0U ? 1U : elapsedNanoseconds;
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

    ++telemetryState_->counters.queryPlanRequests;
    const std::span<const ComponentId> selectedComponentIds{ componentIds, componentCount };
    const std::span<const std::size_t> selectedComponentSizes{ componentSizes, componentCount };
    const auto lookupStartedAt = std::chrono::steady_clock::now();
    if (std::shared_ptr<QueryPlan> cachedPlan = FindCachedQueryPlan(
            selectedComponentIds,
            selectedComponentSizes,
            requiredComponentIds,
            optionalComponentIds,
            excludedComponentIds,
            changedComponentIds)) {
        telemetryState_->counters.queryPlanCacheLookupElapsedNanoseconds += ElapsedNanoseconds(lookupStartedAt);
        ++telemetryState_->counters.queryCacheHits;
        return new QueryState{
            nativeStorage_.get(),
            std::move(cachedPlan),
            config_.executionGrainSize,
            config_.queryPrefetchDistance,
            mutableComponentBorrowLocks_.get(),
            structuralChangeValidator_.get(),
            &telemetryState_->counters,
            &telemetryState_->mutex,
        };
    }

    telemetryState_->counters.queryPlanCacheLookupElapsedNanoseconds += ElapsedNanoseconds(lookupStartedAt);
    ++telemetryState_->counters.queryCacheMisses;
    ++telemetryState_->counters.queryPlanBuilds;
    const auto buildStartedAt = std::chrono::steady_clock::now();
    auto plan = std::make_shared<QueryPlan>(
        selectedComponentIds,
        selectedComponentSizes,
        requiredComponentIds,
        optionalComponentIds,
        excludedComponentIds,
        changedComponentIds);
    if (!plan->IsValid()) {
        telemetryState_->counters.queryPlanBuildElapsedNanoseconds += ElapsedNanoseconds(buildStartedAt);
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
    telemetryState_->counters.queryPlanBuildElapsedNanoseconds += ElapsedNanoseconds(buildStartedAt);
    return new QueryState{
        nativeStorage_.get(),
        std::move(plan),
        config_.executionGrainSize,
        config_.queryPrefetchDistance,
        mutableComponentBorrowLocks_.get(),
        structuralChangeValidator_.get(),
        &telemetryState_->counters,
        &telemetryState_->mutex,
    };
}

std::shared_ptr<QueryPlan> World::FindCachedQueryPlan(
    std::span<const ComponentId> componentIds,
    std::span<const std::size_t> componentSizes,
    std::span<const ComponentId> requiredComponentIds,
    std::span<const ComponentId> optionalComponentIds,
    std::span<const ComponentId> excludedComponentIds,
    std::span<const ComponentId> changedComponentIds) const {
    const std::size_t hash = QueryPlanHash(
        componentIds, componentSizes, requiredComponentIds, optionalComponentIds, excludedComponentIds, changedComponentIds);
    const auto [first, last] = queryPlanIndex_.equal_range(hash);
    for (auto indexed = first; indexed != last; ++indexed) {
        QueryPlanCache::iterator cached = indexed->second;
        const QueryPlanCacheEntry& entry = *cached;
        if (SpanEquals(componentIds, entry.componentIds)
            && SpanEquals(componentSizes, entry.componentSizes)
            && SpanEquals(requiredComponentIds, entry.requiredComponentIds)
            && SpanEquals(optionalComponentIds, entry.optionalComponentIds)
            && SpanEquals(excludedComponentIds, entry.excludedComponentIds)
            && SpanEquals(changedComponentIds, entry.changedComponentIds)) {
            queryPlanCache_.splice(queryPlanCache_.end(), queryPlanCache_, cached);
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
    const std::size_t hash = QueryPlanHash(
        componentIds, componentSizes, requiredComponentIds, optionalComponentIds, excludedComponentIds, changedComponentIds);
    queryPlanCache_.push_back(QueryPlanCacheEntry{
        .hash = hash,
        .componentIds = CopyComponentIds(componentIds),
        .componentSizes = CopyComponentSizes(componentSizes),
        .requiredComponentIds = CopyComponentIds(requiredComponentIds),
        .optionalComponentIds = CopyComponentIds(optionalComponentIds),
        .excludedComponentIds = CopyComponentIds(excludedComponentIds),
        .changedComponentIds = CopyComponentIds(changedComponentIds),
        .plan = std::move(plan),
    });
    const QueryPlanCache::iterator added = std::prev(queryPlanCache_.end());
    try {
        queryPlanIndex_.emplace(hash, added);
    } catch (...) {
        queryPlanCache_.pop_back();
        throw;
    }

    constexpr std::size_t kDefaultQueryPlanCacheCapacity = 1024U;
    const std::size_t capacity = std::max(kDefaultQueryPlanCacheCapacity, config_.reserveQueryCache);
    if (queryPlanCache_.size() > capacity) {
        EraseCachedQueryPlan(queryPlanCache_.begin());
    }
}

void World::ReleaseUnusedQueryPlans() {
    for (auto entry = queryPlanCache_.begin(); entry != queryPlanCache_.end();) {
        if (entry->plan.use_count() == 1L) {
            const auto next = std::next(entry);
            EraseCachedQueryPlan(entry);
            entry = next;
        } else {
            ++entry;
        }
    }
}

void World::EraseCachedQueryPlan(QueryPlanCache::iterator entry) const noexcept {
    const auto [first, last] = queryPlanIndex_.equal_range(entry->hash);
    for (auto indexed = first; indexed != last; ++indexed) {
        if (indexed->second == entry) {
            queryPlanIndex_.erase(indexed);
            break;
        }
    }
    queryPlanCache_.erase(entry);
}

ecs_table_t* World::EntityArchetype(Entity entity) const noexcept {
    if (world_ == nullptr || !entity.IsValid() || !ecs_is_alive(world_, FlecsEntityId(entity))) {
        return nullptr;
    }
    return ecs_get_table(world_, FlecsEntityId(entity));
}

void World::InvalidateQueryPlansForArchetypeChange(ecs_table_t* previousArchetype, ecs_table_t* currentArchetype) noexcept {
    if (previousArchetype != currentArchetype || (previousArchetype == nullptr && currentArchetype == nullptr)) {
        ++telemetryState_->counters.archetypeTransitionInvalidationsSinceReset;
        ++telemetryState_->counters.totalArchetypeTransitionInvalidations;
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
    telemetryState_->counters.structuralChangesSinceReset += count;
    telemetryState_->counters.totalStructuralChanges += count;
}

} // namespace kb::ecs
