#pragma once

// H9 - cohesive render<->ECS bridge telemetry.
//
// Aggregates the two halves of the bridge into one report: how the render side
// consumed transforms (precomputed world vs resolve fallback) and how the
// instance stream applied them (in-place refresh vs full invalidation), plus the
// resident proxy population.

#include "kb/render/scene/EcsRenderSceneSynchronizer.hpp"
#include "kb/render/scene/RenderScene.hpp"

#include <cstdint>
#include <iosfwd>
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

    [[nodiscard]] double PrecomputedReadRatio() const noexcept;
    [[nodiscard]] double InPlaceUpdateRatio() const noexcept;
};

class RenderBridgeTelemetryBuilder final {
  public:
    RenderBridgeTelemetryBuilder() = delete;

    [[nodiscard]] static RenderBridgeTelemetry Build(const EcsRenderSceneSynchronizerStats& syncStats,
                                                     const RenderSceneStats& sceneStats) noexcept;
};

class RenderBridgeTelemetryJsonWriter final {
  public:
    RenderBridgeTelemetryJsonWriter() = delete;

    static void Write(std::ostream& output, const RenderBridgeTelemetry& telemetry);
    [[nodiscard]] static std::string ToString(const RenderBridgeTelemetry& telemetry);
};

} // namespace kb::render
