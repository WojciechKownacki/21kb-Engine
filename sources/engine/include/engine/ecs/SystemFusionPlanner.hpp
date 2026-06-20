#pragma once

// SystemFusionPlanner: a pure planner that decides which co-frame systems can be
// fused into a single chunk pass (load the data once, run several per-entity
// bodies) and which heavy systems should be split into parallel ranges. It
// operates on lightweight descriptors, not live System objects, so it is fully
// testable and decoupled from the scheduler runtime.
//
// Fusion contract: systems fuse only when they iterate the same archetype family
// and none of them imposes a sync point (a structural-change / asset boundary
// that must flush before the next system). Descriptors are expected in execution
// order (already topologically sorted by dependencies), so the fused body simply
// runs them in that order per entity.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace kb::ecs {

struct SystemFusionDescriptor {
    std::string_view name;
    // Identity of the archetype family the system iterates. Systems that touch
    // the same family share a signature; mismatched signatures never fuse.
    std::uint64_t archetypeSignature = 0;
    std::uint64_t estimatedCostNanoseconds = 0;
    std::size_t entityCount = 0;
    // A sync point forces a pass boundary: the system cannot fuse with neighbours.
    bool hasSyncPoint = false;
};

struct SystemFusionPlannerConfig {
    // Systems costlier than this are candidates for range splitting.
    std::uint64_t splitCostThresholdNanoseconds = 250'000; // 0.25 ms
    std::size_t workerCount = 1;
    // Don't split below this many entities -- overhead would dominate.
    std::size_t minEntitiesPerSplit = 4'096;
};

struct SystemFusionGroup {
    std::uint64_t archetypeSignature = 0;
    std::vector<std::size_t> systemIndices;
};

struct SystemSplitRecommendation {
    std::size_t systemIndex = 0;
    std::size_t suggestedRangeCount = 1;
};

struct SystemFusionPlan {
    std::vector<SystemFusionGroup> fusionGroups;
    std::vector<SystemSplitRecommendation> splits;
    std::size_t baselineChunkPasses = 0;
    std::size_t fusedChunkPasses = 0;

    [[nodiscard]] std::size_t ReducedChunkPasses() const noexcept {
        return baselineChunkPasses > fusedChunkPasses ? baselineChunkPasses - fusedChunkPasses : 0U;
    }
};

[[nodiscard]] inline SystemFusionPlan PlanSystemFusion(
    std::span<const SystemFusionDescriptor> systems,
    SystemFusionPlannerConfig config = {}) {
    SystemFusionPlan plan;
    plan.baselineChunkPasses = systems.size();

    // Distinct archetype signatures, in first-seen order, restricted to fusible
    // (sync-point-free) systems.
    std::vector<std::uint64_t> signatures;
    for (std::size_t index = 0; index < systems.size(); ++index) {
        const SystemFusionDescriptor& system = systems[index];

        if (system.estimatedCostNanoseconds >= config.splitCostThresholdNanoseconds &&
            system.entityCount >= config.minEntitiesPerSplit &&
            config.workerCount > 1U) {
            const std::uint64_t ratio = system.estimatedCostNanoseconds / config.splitCostThresholdNanoseconds;
            const std::size_t maxByEntities = system.entityCount / config.minEntitiesPerSplit;
            std::size_t rangeCount = static_cast<std::size_t>(std::max<std::uint64_t>(ratio, 2U));
            rangeCount = std::min(rangeCount, config.workerCount);
            rangeCount = std::min(rangeCount, std::max<std::size_t>(maxByEntities, 1U));
            if (rangeCount >= 2U) {
                plan.splits.push_back(SystemSplitRecommendation{
                    .systemIndex = index,
                    .suggestedRangeCount = rangeCount,
                });
            }
        }

        if (system.hasSyncPoint) {
            continue;
        }
        if (std::find(signatures.begin(), signatures.end(), system.archetypeSignature) == signatures.end()) {
            signatures.push_back(system.archetypeSignature);
        }
    }

    std::size_t fusiblePasses = 0U;
    for (std::uint64_t signature : signatures) {
        SystemFusionGroup group;
        group.archetypeSignature = signature;
        for (std::size_t index = 0; index < systems.size(); ++index) {
            if (!systems[index].hasSyncPoint && systems[index].archetypeSignature == signature) {
                group.systemIndices.push_back(index);
            }
        }
        // A fused pass loads the archetype once regardless of how many bodies run.
        fusiblePasses += 1U;
        if (group.systemIndices.size() >= 2U) {
            plan.fusionGroups.push_back(std::move(group));
        }
    }

    std::size_t syncPasses = 0U;
    for (const SystemFusionDescriptor& system : systems) {
        syncPasses += system.hasSyncPoint ? 1U : 0U;
    }

    plan.fusedChunkPasses = fusiblePasses + syncPasses;
    return plan;
}

} // namespace kb::ecs
