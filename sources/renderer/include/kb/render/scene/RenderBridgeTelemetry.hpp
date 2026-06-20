#pragma once

// H9 - cohesive render<->ECS bridge telemetry.
//
// Aggregates the two halves of the bridge into one report: how the render side
// consumed transforms (precomputed world vs resolve fallback) and how the
// instance stream applied them (in-place refresh vs full invalidation), plus the
// resident proxy population. It is a pure aggregator over the public stats
// structs - no coupling to internals - and exports to JSON for tooling.

#include "kb/render/scene/EcsRenderSceneSynchronizer.hpp"
#include "kb/render/scene/RenderScene.hpp"

#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>

namespace kb::render {

struct RenderBridgeTelemetry {
    // Transform consumption (synchronizer): how many world reads used the
    // precomputed transform vs fell back to a recursive resolve.
    std::uint64_t worldTransformPrecomputedReads = 0;
    std::uint64_t worldTransformResolvedFallbacks = 0;
    // Instance stream (RenderScene): transform-only updates that refreshed an
    // instance in place vs those that forced a draw-group invalidation.
    std::uint64_t transformInPlaceUpdates = 0;
    std::uint64_t transformFallbackUpdates = 0;
    // Resident proxy population.
    std::uint32_t meshProxies = 0;
    std::uint32_t cameraProxies = 0;
    std::uint32_t lightProxies = 0;

    [[nodiscard]] double PrecomputedReadRatio() const noexcept {
        const std::uint64_t total = worldTransformPrecomputedReads + worldTransformResolvedFallbacks;
        return total == 0U ? 1.0 : static_cast<double>(worldTransformPrecomputedReads) / static_cast<double>(total);
    }

    [[nodiscard]] double InPlaceUpdateRatio() const noexcept {
        const std::uint64_t total = transformInPlaceUpdates + transformFallbackUpdates;
        return total == 0U ? 1.0 : static_cast<double>(transformInPlaceUpdates) / static_cast<double>(total);
    }
};

[[nodiscard]] inline RenderBridgeTelemetry BuildRenderBridgeTelemetry(
    const EcsRenderSceneSynchronizerStats& syncStats,
    const RenderSceneStats& sceneStats) noexcept {
    return RenderBridgeTelemetry{
        .worldTransformPrecomputedReads = syncStats.transformPrecomputedReadCount,
        .worldTransformResolvedFallbacks = syncStats.transformResolvedFallbackCount,
        .transformInPlaceUpdates = sceneStats.transformInPlaceUpdateCount,
        .transformFallbackUpdates = sceneStats.transformFallbackUpdateCount,
        .meshProxies = sceneStats.meshProxyCount,
        .cameraProxies = sceneStats.cameraProxyCount,
        .lightProxies = sceneStats.lightProxyCount,
    };
}

inline void ExportRenderBridgeTelemetryToJson(std::ostream& output, const RenderBridgeTelemetry& telemetry) {
    output << "{\n";
    output << "  \"schema\": \"kb.render.bridge_telemetry.v1\",\n";
    output << "  \"world_transform_precomputed_reads\": " << telemetry.worldTransformPrecomputedReads << ",\n";
    output << "  \"world_transform_resolved_fallbacks\": " << telemetry.worldTransformResolvedFallbacks << ",\n";
    output << "  \"precomputed_read_ratio\": " << telemetry.PrecomputedReadRatio() << ",\n";
    output << "  \"transform_in_place_updates\": " << telemetry.transformInPlaceUpdates << ",\n";
    output << "  \"transform_fallback_updates\": " << telemetry.transformFallbackUpdates << ",\n";
    output << "  \"in_place_update_ratio\": " << telemetry.InPlaceUpdateRatio() << ",\n";
    output << "  \"mesh_proxies\": " << telemetry.meshProxies << ",\n";
    output << "  \"camera_proxies\": " << telemetry.cameraProxies << ",\n";
    output << "  \"light_proxies\": " << telemetry.lightProxies << "\n";
    output << "}\n";
}

[[nodiscard]] inline std::string RenderBridgeTelemetryToJsonString(const RenderBridgeTelemetry& telemetry) {
    std::ostringstream stream;
    ExportRenderBridgeTelemetryToJson(stream, telemetry);
    return stream.str();
}

} // namespace kb::render
