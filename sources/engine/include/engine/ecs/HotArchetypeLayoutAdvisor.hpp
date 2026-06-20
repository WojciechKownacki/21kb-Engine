#pragma once

// HotArchetypeLayoutAdvisor: a pure, side-effect-free analyzer that inspects
// captured storage statistics and surfaces layout problems the roadmap calls
// out -- oversized hot archetypes, cold components riding in hot chunks, low
// occupancy, and tag-driven archetype explosion. It depends only on the public
// NativeEcsStorageStats snapshot, never on storage internals, so it can run from
// tooling, tests, or telemetry without coupling.

#include "engine/ecs/NativeArchetypeStorage.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace kb::ecs {

enum class HotArchetypeAdvisoryKind {
    SplitLargeHotArchetype,
    ColdComponentsInHotArchetype,
    LowOccupancyArchetype,
    TagArchetypeExplosion,
};

[[nodiscard]] constexpr std::string_view HotArchetypeAdvisoryKindName(HotArchetypeAdvisoryKind kind) noexcept {
    switch (kind) {
    case HotArchetypeAdvisoryKind::SplitLargeHotArchetype:
        return "split_large_hot_archetype";
    case HotArchetypeAdvisoryKind::ColdComponentsInHotArchetype:
        return "cold_components_in_hot_archetype";
    case HotArchetypeAdvisoryKind::LowOccupancyArchetype:
        return "low_occupancy_archetype";
    case HotArchetypeAdvisoryKind::TagArchetypeExplosion:
        return "tag_archetype_explosion";
    }
    return "unknown";
}

struct HotArchetypeAdvisory {
    HotArchetypeAdvisoryKind kind = HotArchetypeAdvisoryKind::SplitLargeHotArchetype;
    std::size_t archetypeIndex = 0;
    std::size_t componentCount = 0;
    std::size_t hotComponentCount = 0;
    std::size_t coldComponentCount = 0;
    std::size_t tagComponentCount = 0;
    std::size_t liveEntities = 0;
    std::size_t hotBytesPerEntity = 0;
    double occupancy = 0.0;
    std::string_view detail;
};

struct HotArchetypeLayoutAdvisorConfig {
    // A hot archetype is "large" when it carries many hot columns or wide hot
    // rows -- both inflate the working set a hot loop must stream per entity.
    std::size_t largeHotComponentThreshold = 8U;
    std::size_t largeHotBytesPerEntityThreshold = 256U;
    // Occupancy below this (for archetypes spanning multiple chunks) wastes
    // bandwidth on partially filled chunks.
    double lowOccupancyThreshold = 0.5;
    std::size_t lowOccupancyMinChunks = 2U;
    // Tag explosion: many near-empty tag-bearing archetypes indicate tags that
    // should be sparse state rather than archetype-defining components.
    std::size_t tagExplosionMinArchetypes = 16U;
    double tagExplosionFraction = 0.5;
    std::size_t tagExplosionMaxLiveEntities = 8U;
};

struct HotArchetypeLayoutReport {
    std::vector<HotArchetypeAdvisory> advisories;
    std::size_t analyzedArchetypes = 0;
    std::size_t tagHeavyArchetypes = 0;

    [[nodiscard]] bool Empty() const noexcept {
        return advisories.empty();
    }

    [[nodiscard]] std::size_t CountOfKind(HotArchetypeAdvisoryKind kind) const noexcept {
        std::size_t count = 0U;
        for (const HotArchetypeAdvisory& advisory : advisories) {
            count += advisory.kind == kind ? 1U : 0U;
        }
        return count;
    }
};

[[nodiscard]] inline HotArchetypeLayoutReport AnalyzeHotArchetypeLayout(
    const NativeEcsStorageStats& stats,
    HotArchetypeLayoutAdvisorConfig config = {}) {
    HotArchetypeLayoutReport report;
    std::size_t tagHeavyNearEmpty = 0U;

    for (const NativeEcsArchetypeMemoryCounters& archetype : stats.archetypeCounters) {
        if (archetype.capacity == 0U && archetype.liveEntities == 0U) {
            continue;
        }
        ++report.analyzedArchetypes;

        const std::size_t componentCount = archetype.componentIds.size();
        const std::size_t hotBytesPerEntity = archetype.capacity == 0U
            ? 0U
            : archetype.hotTableCapacityBytes / archetype.capacity;
        const double occupancy = archetype.capacity == 0U
            ? 0.0
            : static_cast<double>(archetype.liveEntities) / static_cast<double>(archetype.capacity);

        const auto makeAdvisory = [&](HotArchetypeAdvisoryKind kind, std::string_view detail) {
            report.advisories.push_back(HotArchetypeAdvisory{
                .kind = kind,
                .archetypeIndex = archetype.archetypeIndex,
                .componentCount = componentCount,
                .hotComponentCount = archetype.hotTableComponents,
                .coldComponentCount = archetype.coldTableComponents,
                .tagComponentCount = archetype.sparseTagComponents,
                .liveEntities = archetype.liveEntities,
                .hotBytesPerEntity = hotBytesPerEntity,
                .occupancy = occupancy,
                .detail = detail,
            });
        };

        if (archetype.hotTableComponents >= config.largeHotComponentThreshold ||
            hotBytesPerEntity >= config.largeHotBytesPerEntityThreshold) {
            makeAdvisory(
                HotArchetypeAdvisoryKind::SplitLargeHotArchetype,
                "hot archetype is wide; split rarely co-accessed components into a separate archetype family");
        }

        if (archetype.hotTableComponents > 0U && archetype.coldTableComponents > 0U) {
            makeAdvisory(
                HotArchetypeAdvisoryKind::ColdComponentsInHotArchetype,
                "cold components share hot chunks; move seldom-iterated data to SparsePayload/ExternalBlob");
        }

        if (archetype.chunks >= config.lowOccupancyMinChunks && occupancy < config.lowOccupancyThreshold) {
            makeAdvisory(
                HotArchetypeAdvisoryKind::LowOccupancyArchetype,
                "archetype occupancy is low; compaction or chunk-size tuning would reclaim bandwidth");
        }

        const bool tagBearing = archetype.sparseTagComponents > 0U;
        const bool nearEmpty = archetype.liveEntities <= config.tagExplosionMaxLiveEntities;
        if (tagBearing && nearEmpty) {
            ++tagHeavyNearEmpty;
        }
    }

    report.tagHeavyArchetypes = tagHeavyNearEmpty;
    if (report.analyzedArchetypes >= config.tagExplosionMinArchetypes &&
        tagHeavyNearEmpty >= config.tagExplosionMinArchetypes &&
        static_cast<double>(tagHeavyNearEmpty) >=
            config.tagExplosionFraction * static_cast<double>(report.analyzedArchetypes)) {
        report.advisories.push_back(HotArchetypeAdvisory{
            .kind = HotArchetypeAdvisoryKind::TagArchetypeExplosion,
            .liveEntities = 0U,
            .detail = "many near-empty tag-bearing archetypes; prefer SparseTag state over archetype-defining tags",
        });
    }

    return report;
}

} // namespace kb::ecs
