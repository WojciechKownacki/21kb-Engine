#include "ecs/QueryStateFactory.hpp"

#include "ecs/QueryState.hpp"
#include "ecs/query/QueryPlan.hpp"

#include <memory>
#include <utility>

namespace kb::ecs {

QueryState* QueryStateFactory::Create(
    NativeArchetypeStorage* nativeStorage,
    std::span<const ComponentId> componentIds,
    std::span<const std::size_t> componentSizes,
    std::span<const ComponentId> requiredComponentIds,
    std::span<const ComponentId> optionalComponentIds,
    std::span<const ComponentId> excludedComponentIds,
    std::span<const ComponentId> changedComponentIds,
    const WorldConfig& config,
    MutableComponentBorrowLocks* mutableBorrowLocks,
    StructuralChangeValidator* structuralChangeValidator) {
    if (nativeStorage == nullptr || componentIds.empty() || componentIds.size() != componentSizes.size()) {
        return nullptr;
    }

    auto plan = std::make_shared<QueryPlan>(
        componentIds,
        componentSizes,
        requiredComponentIds,
        optionalComponentIds,
        excludedComponentIds,
        changedComponentIds);
    if (!plan->IsValid()) {
        return nullptr;
    }

    return new QueryState{ nativeStorage, std::move(plan), config.executionGrainSize, mutableBorrowLocks, structuralChangeValidator, nullptr };
}

} // namespace kb::ecs
