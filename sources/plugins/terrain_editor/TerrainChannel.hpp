#pragma once

#include "TerrainBrush.hpp"
#include "engine/assets/TerrainAsset.hpp"
#include "engine/scene/RiverGeometry.hpp"

#include <span>
#include <vector>

namespace kb::terrain_editor {

// The shape a ramp and a river bed share: a strip along a line whose height follows the line and whose
// edges blend into the ground smoothly. The ramp is one segment with a flat cross section; a river is a
// polyline whose cross section dips to its bed.

// The point of the segment a-b nearest to (x, z): its place along the segment (0 at a, 1 at b) and the distance.
struct TerrainSegmentProbe {
    float along = 0.0F;
    float distance = 0.0F;
};

[[nodiscard]] TerrainSegmentProbe ProbeTerrainSegment(float ax, float az, float bx, float bz, float x, float z) noexcept;

// 1 up to `flatDistance` from the line, then a smooth fall to 0 at `outerDistance`.
[[nodiscard]] float TerrainChannelEdgeWeight(float distance, float flatDistance, float outerDistance) noexcept;

// A station of a channel in the terrain plane (x, z from the centre, in world units): the water level there,
// half the width of the water, and how far the bed lies below the level in the middle.
struct TerrainChannelNode {
    float x = 0.0F;
    float z = 0.0F;
    float level = 0.0F;
    float halfWidth = 1.0F;
    float depth = 0.0F;
};

struct TerrainChannelProbe {
    float distance = 0.0F;
    float level = 0.0F;
    float halfWidth = 0.0F;
    float depth = 0.0F;
};

// The nearest point of the line through the nodes to (x, z), with the level, width and depth interpolated there.
[[nodiscard]] TerrainChannelProbe ProbeTerrainChannel(std::span<const TerrainChannelNode> nodes, float x, float z) noexcept;

struct TerrainChannelSettings {
    // The shoulder on each side of the water that blends the ground to the water level.
    float bankWidth = 3.0F;
};

[[nodiscard]] bool IsTerrainChannelValid(std::span<const TerrainChannelNode> nodes, const TerrainChannelSettings& settings) noexcept;

// The ground height the channel asks for at a probe: the bed across the water (deepest in the middle, at the
// level on the edge), the water level on the bank. `weight` is how much of it replaces the ground there.
[[nodiscard]] float TerrainChannelHeight(const TerrainChannelProbe& probe, const TerrainChannelSettings& settings, float& weight) noexcept;

// The channel of a river: its samples every `step` units, in the plane of a terrain that stands at `terrain`
// while the river stands at `river`, with the half width and the depth of the water at each. Empty for an invalid river.
[[nodiscard]] std::vector<TerrainChannelNode> RiverChannelNodes(const kb::scene::RiverComponent& river, const kb::scene::RiverPlacement& riverPlacement,
    const kb::scene::RiverPlacement& terrainPlacement, float step);

// Cuts the channel into the terrain: inside the water the ground becomes the bed, on the banks it eases to the
// water level, and beyond bankWidth from the water nothing changes. Rows are shared out over the cores.
[[nodiscard]] TerrainBrushResult CarveTerrainChannel(
    kb::assets::TerrainAsset& terrain, std::span<const TerrainChannelNode> nodes, const TerrainChannelSettings& settings) noexcept;

} // namespace kb::terrain_editor
