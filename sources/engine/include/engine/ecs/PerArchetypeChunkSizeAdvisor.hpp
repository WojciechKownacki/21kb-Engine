#pragma once

// PerArchetypeChunkSizeAdvisor: lifts the single-archetype ChunkSizeAutoTuner to
// a per-archetype-family plan. Each archetype is tuned independently against its
// own component layout, entity population, and workload, then the results are
// aggregated into a dominant profile (entity-weighted) so a single-profile world
// can still pick the best global compromise while tooling can see where a
// per-archetype layout would help. Pure and header-only: depends only on the
// existing tuner, never on storage internals.

#include "engine/ecs/ChunkSizeAutoTuner.hpp"
#include "engine/ecs/ChunkSizeProfile.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace kb::ecs {

struct ArchetypeChunkSizeRequest {
    std::size_t archetypeIndex = 0;
    std::span<const NativeComponentType> components;
    std::size_t entityCount = 0;
    ChunkSizeTuningWorkload workload = ChunkSizeTuningWorkload::SequentialHotLoop;
    std::size_t maxChunkPayloadBytes = 0;
    double targetOccupancyPercent = 92.0;
};

struct ArchetypeChunkSizeRecommendation {
    std::size_t archetypeIndex = 0;
    ChunkSizeProfile profile = kDefaultChunkSizeProfile;
    std::size_t entityCount = 0;
    std::size_t estimatedWastedBytes = 0;
    double occupancyPercent = 0.0;
    bool applied = false;
};

struct PerArchetypeChunkSizePlan {
    std::vector<ArchetypeChunkSizeRecommendation> recommendations;
    ChunkSizeProfile dominantProfile = kDefaultChunkSizeProfile;
    bool uniform = true;

    [[nodiscard]] bool Empty() const noexcept {
        return recommendations.empty();
    }
};

[[nodiscard]] inline PerArchetypeChunkSizePlan PlanPerArchetypeChunkSizes(
    std::span<const ArchetypeChunkSizeRequest> requests) {
    PerArchetypeChunkSizePlan plan;
    plan.recommendations.reserve(requests.size());

    std::array<std::size_t, static_cast<std::size_t>(ChunkSizeProfile::Count)> weightByProfile{};
    bool sawAny = false;
    ChunkSizeProfile firstProfile = kDefaultChunkSizeProfile;

    for (const ArchetypeChunkSizeRequest& request : requests) {
        const ChunkSizeTuningResult tuning = TuneChunkSizeProfile(ChunkSizeTuningInput{
            .components = request.components,
            .entityCount = request.entityCount,
            .workload = request.workload,
            .maxChunkPayloadBytes = request.maxChunkPayloadBytes,
            .targetOccupancyPercent = request.targetOccupancyPercent,
        });
        const ChunkSizeTuningCandidate& recommendation = tuning.recommendation;

        ArchetypeChunkSizeRecommendation entry{
            .archetypeIndex = request.archetypeIndex,
            .profile = recommendation.profile,
            .entityCount = request.entityCount,
            .estimatedWastedBytes = recommendation.estimatedWastedBytes,
            .occupancyPercent = recommendation.occupancyPercent,
            .applied = recommendation.valid,
        };
        plan.recommendations.push_back(entry);

        if (!recommendation.valid) {
            continue;
        }

        // Entity-weighted vote, with a floor of one so empty-but-valid archetypes
        // still contribute to the global choice.
        const std::size_t weight = request.entityCount == 0U ? 1U : request.entityCount;
        weightByProfile[static_cast<std::size_t>(recommendation.profile)] += weight;
        if (!sawAny) {
            firstProfile = recommendation.profile;
            sawAny = true;
        } else if (recommendation.profile != firstProfile) {
            plan.uniform = false;
        }
    }

    if (sawAny) {
        std::size_t bestWeight = 0U;
        ChunkSizeProfile bestProfile = firstProfile;
        for (std::size_t index = 0; index < weightByProfile.size(); ++index) {
            if (weightByProfile[index] > bestWeight) {
                bestWeight = weightByProfile[index];
                bestProfile = static_cast<ChunkSizeProfile>(index);
            }
        }
        plan.dominantProfile = bestProfile;
    } else {
        plan.uniform = true;
    }

    return plan;
}

} // namespace kb::ecs
