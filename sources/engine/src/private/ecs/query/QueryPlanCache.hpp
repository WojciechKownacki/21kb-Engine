#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "ecs/query/QueryPlan.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

struct ecs_world_t;
struct ecs_table_t;

namespace kb::ecs {

class QueryPlanCache {
public:
    [[nodiscard]] std::shared_ptr<QueryPlan> GetOrCreate(
        ecs_world_t* world,
        std::span<const ComponentId> componentIds,
        std::span<const std::size_t> componentSizes,
        std::span<const ComponentId> requiredComponentIds,
        std::span<const ComponentId> optionalComponentIds,
        std::span<const ComponentId> excludedComponentIds);

    void Clear() noexcept;
    void InvalidateArchetype(ecs_table_t* archetype) noexcept;
    void InvalidateTouchedArchetypes(ecs_table_t* previousArchetype, ecs_table_t* currentArchetype) noexcept;

private:
    struct TermKey {
        ComponentId componentId = 0;
        std::size_t componentSize = 0;
        std::uint8_t operatorKind = 0;

        [[nodiscard]] bool operator==(const TermKey& other) const noexcept;
    };

    struct PlanKey {
        std::vector<TermKey> terms;

        [[nodiscard]] bool operator==(const PlanKey& other) const noexcept;
    };

    struct PlanKeyHash {
        [[nodiscard]] std::size_t operator()(const PlanKey& key) const noexcept;
    };

    struct CachedPlan {
        std::shared_ptr<QueryPlan> plan;
        std::vector<const ecs_table_t*> matchedArchetypes;
    };

    [[nodiscard]] static bool CanBuildKey(
        std::span<const ComponentId> componentIds,
        std::span<const std::size_t> componentSizes,
        std::span<const ComponentId> requiredComponentIds,
        std::span<const ComponentId> optionalComponentIds,
        std::span<const ComponentId> excludedComponentIds) noexcept;
    [[nodiscard]] static PlanKey BuildKey(
        std::span<const ComponentId> componentIds,
        std::span<const std::size_t> componentSizes,
        std::span<const ComponentId> requiredComponentIds,
        std::span<const ComponentId> optionalComponentIds,
        std::span<const ComponentId> excludedComponentIds);
    [[nodiscard]] static std::vector<const ecs_table_t*> CollectMatchedArchetypes(ecs_world_t* world, ecs_query_t* query);
    [[nodiscard]] static bool CachedPlanTouchesArchetype(const CachedPlan& cachedPlan, ecs_table_t* archetype) noexcept;

    std::unordered_map<PlanKey, CachedPlan, PlanKeyHash> plans_;
};

} // namespace kb::ecs
