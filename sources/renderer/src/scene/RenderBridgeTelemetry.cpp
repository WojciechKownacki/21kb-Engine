#include "kb/render/scene/RenderBridgeTelemetry.hpp"

#include <ostream>
#include <sstream>

namespace kb::render {

double RenderBridgeTelemetry::PrecomputedReadRatio() const noexcept {
    const std::uint64_t total = worldTransformPrecomputedReads + worldTransformResolvedFallbacks;
    return total == 0U ? 1.0 : static_cast<double>(worldTransformPrecomputedReads) / static_cast<double>(total);
}

double RenderBridgeTelemetry::InPlaceUpdateRatio() const noexcept {
    const std::uint64_t total = transformInPlaceUpdates + transformFallbackUpdates;
    return total == 0U ? 1.0 : static_cast<double>(transformInPlaceUpdates) / static_cast<double>(total);
}

RenderBridgeTelemetry RenderBridgeTelemetryBuilder::Build(const EcsRenderSceneSynchronizerStats& syncStats,
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

void RenderBridgeTelemetryJsonWriter::Write(std::ostream& output, const RenderBridgeTelemetry& telemetry) {
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

std::string RenderBridgeTelemetryJsonWriter::ToString(const RenderBridgeTelemetry& telemetry) {
    std::ostringstream stream;
    Write(stream, telemetry);
    return stream.str();
}

} // namespace kb::render
