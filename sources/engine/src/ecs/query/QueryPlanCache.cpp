#include "ecs/query/QueryPlanCache.hpp"

#include "ecs/query/QueryLimits.hpp"

#include <flecs.h>

#include <algorithm>
#include <functional>

namespace kb::ecs {
namespace {

[[nodiscard]] std::size_t CombineHash(std::size_t seed, std::size_t value) noexcept {
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

} // namespace

bool QueryPlanCache::TermKey::operator==(const TermKey& other) const noexcept {
    return componentId == other.componentId && componentSize == other.componentSize && operatorKind == other.operatorKind;
}

bool QueryPlanCache::PlanKey::operator==(const PlanKey& other) const noexcept {
    return terms == other.terms;
}

std::size_t QueryPlanCache::PlanKeyHash::operator()(const PlanKey& key) const noexcept {
    std::size_t seed = 0;
    for (const TermKey& term : key.terms) {
        seed = CombineHash(seed, std::hash<ComponentId>{}(term.componentId));
        seed = CombineHash(seed, std::hash<std::size_t>{}(term.componentSize));
        seed = CombineHash(seed, std::hash<std::uint8_t>{}(term.operatorKind));
    }
    return seed;
}

std::shared_ptr<QueryPlan> QueryPlanCache::GetOrCreate(
    ecs_world_t* world,
    std::span<const ComponentId> componentIds,
    std::span<const std::size_t> componentSizes,
    std::span<const ComponentId> requiredComponentIds,
    std::span<const ComponentId> optionalComponentIds,
    std::span<const ComponentId> excludedComponentIds) {
    if (world == nullptr || !CanBuildKey(componentIds, componentSizes, requiredComponentIds, optionalComponentIds, excludedComponentIds)) {
        return {};
    }

    PlanKey key = BuildKey(componentIds, componentSizes, requiredComponentIds, optionalComponentIds, excludedComponentIds);
    const auto existing = plans_.find(key);
    if (existing != plans_.end()) {
        return existing->second.plan;
    }

    auto plan = std::make_shared<QueryPlan>(world, componentIds, componentSizes, requiredComponentIds, optionalComponentIds, excludedComponentIds, std::span<const ComponentId>{});
    if (!plan->IsValid()) {
        return {};
    }

    CachedPlan cachedPlan{
        .plan = plan,
        .matchedArchetypes = CollectMatchedArchetypes(world, plan->Native()),
    };

    const auto result = plans_.emplace(std::move(key), std::move(cachedPlan));
    return result.first->second.plan;
}

void QueryPlanCache::Clear() noexcept {
    plans_.clear();
}

void QueryPlanCache::InvalidateArchetype(ecs_table_t* archetype) noexcept {
    if (archetype == nullptr) {
        return;
    }

    for (auto iterator = plans_.begin(); iterator != plans_.end();) {
        if (CachedPlanTouchesArchetype(iterator->second, archetype)) {
            iterator = plans_.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void QueryPlanCache::InvalidateTouchedArchetypes(ecs_table_t* previousArchetype, ecs_table_t* currentArchetype) noexcept {
    InvalidateArchetype(previousArchetype);
    if (currentArchetype != previousArchetype) {
        InvalidateArchetype(currentArchetype);
    }
}

bool QueryPlanCache::CanBuildKey(
    std::span<const ComponentId> componentIds,
    std::span<const std::size_t> componentSizes,
    std::span<const ComponentId> requiredComponentIds,
    std::span<const ComponentId> optionalComponentIds,
    std::span<const ComponentId> excludedComponentIds) noexcept {
    const std::size_t termCount = componentIds.size() + requiredComponentIds.size() + optionalComponentIds.size() + excludedComponentIds.size();
    if (componentIds.empty() || componentIds.size() != componentSizes.size() || termCount > kMaxQueryTerms) {
        return false;
    }

    for (std::size_t index = 0; index < componentIds.size(); ++index) {
        if (componentIds[index] == 0 || componentSizes[index] == 0) {
            return false;
        }
    }

    for (ComponentId componentId : requiredComponentIds) {
        if (componentId == 0) {
            return false;
        }
    }

    for (ComponentId componentId : optionalComponentIds) {
        if (componentId == 0) {
            return false;
        }
    }

    for (ComponentId componentId : excludedComponentIds) {
        if (componentId == 0) {
            return false;
        }
    }

    return true;
}

QueryPlanCache::PlanKey QueryPlanCache::BuildKey(
    std::span<const ComponentId> componentIds,
    std::span<const std::size_t> componentSizes,
    std::span<const ComponentId> requiredComponentIds,
    std::span<const ComponentId> optionalComponentIds,
    std::span<const ComponentId> excludedComponentIds) {
    PlanKey key;
    key.terms.reserve(componentIds.size() + requiredComponentIds.size() + optionalComponentIds.size() + excludedComponentIds.size());
    for (std::size_t index = 0; index < componentIds.size(); ++index) {
        key.terms.push_back(TermKey{
            .componentId = componentIds[index],
            .componentSize = componentSizes[index],
            .operatorKind = 0,
        });
    }
    for (ComponentId componentId : requiredComponentIds) {
        key.terms.push_back(TermKey{
            .componentId = componentId,
            .operatorKind = 0,
        });
    }
    for (ComponentId componentId : optionalComponentIds) {
        key.terms.push_back(TermKey{
            .componentId = componentId,
            .operatorKind = 1,
        });
    }
    for (ComponentId componentId : excludedComponentIds) {
        key.terms.push_back(TermKey{
            .componentId = componentId,
            .operatorKind = 2,
        });
    }
    return key;
}

std::vector<const ecs_table_t*> QueryPlanCache::CollectMatchedArchetypes(ecs_world_t* world, ecs_query_t* query) {
    if (world == nullptr || query == nullptr) {
        return {};
    }

    std::vector<const ecs_table_t*> archetypes;
    ecs_iter_t iterator = ecs_query_iter(world, query);
    while (ecs_query_next(&iterator)) {
        if (iterator.table != nullptr && std::find(archetypes.begin(), archetypes.end(), iterator.table) == archetypes.end()) {
            archetypes.push_back(iterator.table);
        }
    }
    return archetypes;
}

bool QueryPlanCache::CachedPlanTouchesArchetype(const CachedPlan& cachedPlan, ecs_table_t* archetype) noexcept {
    if (std::find(cachedPlan.matchedArchetypes.begin(), cachedPlan.matchedArchetypes.end(), archetype) != cachedPlan.matchedArchetypes.end()) {
        return true;
    }
    return cachedPlan.plan && cachedPlan.plan->MatchesArchetype(archetype);
}

} // namespace kb::ecs
