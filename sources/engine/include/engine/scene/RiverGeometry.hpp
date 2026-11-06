#pragma once

#include "engine/scene/RiverComponent.hpp"
#include "engine/scene/SceneSurfaceGeometry.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

// One station along a river spline. The position is the water surface, `direction` the unit flow
// direction in the horizontal plane, and `distance` the length along the river from its first point.
struct RiverSample {
    Vec3 position{};
    Vec3 direction{ 1.0F, 0.0F, 0.0F };
    float width = 0.0F;
    float depth = 0.0F;
    float speed = 0.0F;
    float distance = 0.0F;
};

// Stations about `maxStep` apart (never more than `maxSamples` in all) along the river, in the river's local
// space: x and z follow a Catmull-Rom spline through the points, the height, width, depth and speed
// change linearly between neighbouring points, so a steep drop between two points stays one straight fall.
// False for an invalid river.
[[nodiscard]] bool SampleRiver(const RiverComponent& river, float maxStep, std::uint32_t maxSamples, std::vector<RiverSample>& samples);

// A stretch of samples [first, last] whose slope is at least the river's waterfall slope.
struct RiverWaterfall {
    std::uint32_t first = 0U;
    std::uint32_t last = 0U;
    float topHeight = 0.0F;
    float bottomHeight = 0.0F;
};

// The steep stretches of a sampled river (a drop of less than half a unit is not a waterfall).
[[nodiscard]] std::vector<RiverWaterfall> FindRiverWaterfalls(std::span<const RiverSample> samples, float slopeDegrees);

// Where an entity stands in the world; a river and the terrain it is cut into are put in one frame with it.
struct RiverPlacement {
    Vec3 position{};
    Quat rotation{};
    Vec3 scale{ 1.0F, 1.0F, 1.0F };
};

// The river sampled as a polyline in the local space of another entity (a terrain): the position of each
// sample there, its width and depth scaled by the ratio of the horizontal scales.
[[nodiscard]] bool SampleRiverInFrame(const RiverComponent& river, const RiverPlacement& riverPlacement, const RiverPlacement& frame,
    float maxStep, std::uint32_t maxSamples, std::vector<RiverSample>& samples);

// The water of a river as two surfaces: the stream, and the waterfalls (steep stretches, with their own
// material). Vertex layout, for the water materials: u runs 0 to 1 across the water (0.5 on the middle), v is the
// distance along it in units of kRiverUvLength; the normal carries the current: its x and z are the flow
// velocity (in units per second) divided by kRiverFlowScale, y completes the unit vector.
inline constexpr float kRiverUvLength = 4.0F;
inline constexpr float kRiverFlowScale = 10.0F;

struct RiverSurface {
    std::vector<SurfaceGeometryVertex> streamVertices;
    std::vector<std::uint32_t> streamIndices;
    std::vector<SurfaceGeometryVertex> fallVertices;
    std::vector<std::uint32_t> fallIndices;
    std::vector<RiverSample> samples;
    std::vector<RiverWaterfall> waterfalls;
};

[[nodiscard]] bool BuildRiverSurface(const RiverComponent& river, RiverSurface& surface);

// The points as text, "x,y,z,width,depth,speed;..." and back. A point of text takes 3 numbers (a position; the
// point keeps the default width, depth and speed), or all 6.
[[nodiscard]] std::string RiverPointsToText(const RiverComponent& river);
// Any number of points up to MaxPoints (one point is a list as well: the tools add points one by one).
[[nodiscard]] bool ParseRiverPointList(std::string_view text, std::vector<RiverPoint>& points, std::string& error);
// A whole river: 2 to MaxPoints points; changes `river.points` and `pointCount` only.
[[nodiscard]] bool ParseRiverPointsText(std::string_view text, RiverComponent& river, std::string& error);
// Writes the points to the river; false, with the reason, when there are too few or too many.
[[nodiscard]] bool SetRiverPoints(RiverComponent& river, std::span<const RiverPoint> points, std::string& error);

} // namespace kb::scene
