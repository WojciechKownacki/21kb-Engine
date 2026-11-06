// Rivers: the channel profile shared with the ramp, the river points and waterfalls the engine derives from them.
#include "TerrainAnalysis.hpp"
#include "TerrainChannel.hpp"
#include "TerrainOps.hpp"

#include "engine/core/JsonValue.hpp"
#include "engine/scene/RiverComponent.hpp"
#include "engine/scene/RiverGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using kb::terrain_editor::TerrainChannelNode;
using kb::terrain_editor::TerrainChannelSettings;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

[[nodiscard]] float HeightAt(const kb::assets::TerrainAsset& terrain, float x, float z) {
    return kb::terrain_editor::SampleTerrainHeight(terrain, x, z);
}

// A river along x with a level of 3 that sinks to 1 at the far end, on ground that is 8 high.
[[nodiscard]] std::vector<TerrainChannelNode> StraightChannel() {
    return { { -40.0F, 0.0F, 3.0F, 3.0F, 1.0F }, { 0.0F, 0.0F, 3.0F, 3.0F, 1.0F }, { 40.0F, 0.0F, 1.0F, 3.0F, 1.0F } };
}

[[nodiscard]] kb::assets::TerrainAsset HighGround() {
    kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset(129U, 128.0F, 128.0F);
    std::fill(terrain.heights.begin(), terrain.heights.end(), 8.0F);
    return terrain;
}

void RunChannelProfileTest() {
    kb::assets::TerrainAsset terrain = HighGround();
    const kb::assets::TerrainAsset before = terrain;
    const TerrainChannelSettings settings{ .bankWidth = 4.0F };
    const std::vector<TerrainChannelNode> nodes = StraightChannel();
    Require(kb::terrain_editor::CarveTerrainChannel(terrain, nodes, settings).Changed(), "the channel changed the terrain");
    // Across the water: the bed is deepest in the middle, a parabola that reaches the level at the edge.
    Require(std::abs(HeightAt(terrain, -20.0F, 0.0F) - 2.0F) < 0.02F, "the bed lies one unit under the level in the middle");
    Require(std::abs(HeightAt(terrain, -20.0F, 1.5F) - 2.25F) < 0.06F, "the bed rises like a parabola towards the edge");
    Require(std::abs(HeightAt(terrain, -20.0F, 3.0F) - 3.0F) < 0.06F, "the water edge is at the water level");
    Require(std::abs(HeightAt(terrain, -20.0F, 1.5F) - HeightAt(terrain, -20.0F, -1.5F)) < 0.001F, "both sides of the bed are alike");
    // The level follows the river: it sinks by 2 units over the second half.
    Require(std::abs(HeightAt(terrain, 20.0F, 0.0F) - 1.0F) < 0.03F, "the bed follows the sinking level (mid way 2 - 1)");
    Require(HeightAt(terrain, 40.0F, 0.0F) < HeightAt(terrain, 0.0F, 0.0F) - 1.5F, "downstream the bed is lower");
    // The banks ease up to the old ground and never above it, without a step.
    float previous = HeightAt(terrain, -20.0F, 3.0F);
    float largestStep = 0.0F;
    for (float distance = 3.25F; distance <= 7.5F; distance += 0.25F) {
        const float height = HeightAt(terrain, -20.0F, distance);
        Require(height >= previous - 0.0005F, "the bank only rises away from the water");
        Require(height <= 8.0F + 0.0005F, "the bank never rises above the ground it eases into");
        largestStep = std::max(largestStep, height - previous);
        previous = height;
    }
    Require(largestStep < 1.2F, "the bank has no step");
    Require(std::abs(previous - 8.0F) < 0.001F, "beyond the bank the ground is as it was");
    // Nothing changes beyond the water and its bank, in either direction.
    for (std::uint32_t z = 0U; z < terrain.height; ++z) {
        const float worldZ = static_cast<float>(z) * terrain.worldSizeZ / static_cast<float>(terrain.height - 1U) - terrain.worldSizeZ * 0.5F;
        if (std::abs(worldZ) <= 7.01F) continue;
        for (std::uint32_t x = 0U; x < terrain.width; ++x) {
            Require(terrain.heights[static_cast<std::size_t>(z) * terrain.width + x] == before.heights[static_cast<std::size_t>(z) * terrain.width + x],
                "the terrain beyond the bank is untouched");
        }
    }
    // Low ground beside the water is raised to the level on the bank so that the water has banks.
    kb::assets::TerrainAsset low = kb::assets::MakeFlatTerrainAsset(129U, 128.0F, 128.0F);
    Require(kb::terrain_editor::CarveTerrainChannel(low, nodes, settings).Changed(), "the channel changed low ground");
    Require(HeightAt(low, -20.0F, 3.0F) > 2.9F && HeightAt(low, -20.0F, 8.0F) == 0.0F, "ground below the level is raised to it at the water");
}

void RunChannelRejectionTest() {
    kb::assets::TerrainAsset terrain = HighGround();
    const TerrainChannelSettings settings{};
    std::vector<TerrainChannelNode> nodes = StraightChannel();
    Require(!kb::terrain_editor::CarveTerrainChannel(terrain, std::span<const TerrainChannelNode>{ nodes.data(), 1U }, settings).Changed(), "one station is not a channel");
    nodes[1].halfWidth = 0.0F;
    Require(!kb::terrain_editor::CarveTerrainChannel(terrain, nodes, settings).Changed(), "a channel without width is refused");
    nodes = StraightChannel();
    nodes[0].depth = -1.0F;
    Require(!kb::terrain_editor::CarveTerrainChannel(terrain, nodes, settings).Changed(), "a negative depth is refused");
    nodes = StraightChannel();
    for (TerrainChannelNode& node : nodes) node.x += 1000.0F;
    Require(!kb::terrain_editor::CarveTerrainChannel(terrain, nodes, settings).Changed(), "a channel beside the terrain changes nothing");
    Require(terrain.heights == HighGround().heights, "a refused channel leaves the terrain as it was");
}

void RunChannelOperationTest() {
    kb::assets::TerrainAsset terrain = HighGround();
    kb::core::JsonValue operation;
    std::string error;
    Require(kb::core::JsonValue::Parse(R"({"op":"channel","nodes":[[-40,0,3,3,1],[40,0,3,3,1]],"bank":2})", operation, error), "channel operation parses");
    kb::terrain_editor::TerrainOpContext context;
    kb::terrain_editor::TerrainOpSummary summary;
    Require(kb::terrain_editor::ApplyTerrainOp(terrain, operation, context, summary, error), error.c_str());
    Require(summary.changedSamples > 0U && std::abs(HeightAt(terrain, 0.0F, 0.0F) - 2.0F) < 0.02F, "the channel operation cuts the bed");
    Require(kb::core::JsonValue::Parse(R"({"op":"channel","nodes":[[-40,0,3,3]]})", operation, error), "bad channel parses");
    Require(!kb::terrain_editor::ApplyTerrainOp(terrain, operation, context, summary, error) && error.find("node 0") != std::string::npos,
        "a node with four numbers is named");
    Require(kb::core::JsonValue::Parse(R"({"op":"channel","nodes":[[0,0,3,3,1],[9,0,3,3,1]],"width":4})", operation, error), "unknown member parses");
    Require(!kb::terrain_editor::ApplyTerrainOp(terrain, operation, context, summary, error) && error.find("width") != std::string::npos,
        "a misspelled member is an error");
}

[[nodiscard]] kb::scene::RiverComponent FallingRiver() {
    kb::scene::RiverComponent river{};
    river.pointCount = 4U;
    river.points[0] = { { 0.0F, 10.0F, 0.0F }, 4.0F, 1.0F, 1.5F };
    river.points[1] = { { 20.0F, 10.0F, 0.0F }, 4.0F, 1.0F, 1.5F };
    river.points[2] = { { 24.0F, 2.0F, 0.0F }, 4.0F, 1.0F, 3.0F };
    river.points[3] = { { 44.0F, 2.0F, 0.0F }, 4.0F, 1.0F, 1.5F };
    return river;
}

void RunRiverPointsTest() {
    kb::scene::RiverComponent river = FallingRiver();
    Require(kb::scene::IsRiverComponentValid(river), "the example river is valid");
    // Text round trip, the format of kb_cli component set and the editor tool.
    const std::string text = kb::scene::RiverPointsToText(river);
    kb::scene::RiverComponent parsed{};
    std::string error;
    Require(kb::scene::ParseRiverPointsText(text, parsed, error), error.c_str());
    Require(parsed.pointCount == 4U && parsed == river, "the points survive their text");
    Require(kb::scene::ParseRiverPointsText("0,0,0;5,0,0;10,0,2,6,2,0.5", parsed, error) && parsed.pointCount == 3U &&
            parsed.points[1].width == kb::scene::RiverPoint{}.width && parsed.points[2].depth == 2.0F,
        "a point may give a position only");
    for (const char* bad : { "0,0,0", "0,0,0;1,0", "0,0,0;x,0,0", "0,0,0;1,0,0,0,1,1", "0,0,0;1,0,0,2,-1,1", "0,0,0;1,0,0,1,1,1,1" }) {
        Require(!kb::scene::ParseRiverPointsText(bad, parsed, error) && !error.empty(), bad);
    }
    // A river keeps its limit of points and states it.
    std::string many;
    for (int index = 0; index < 33; ++index) many += (index != 0 ? ";" : "") + std::to_string(index) + ",0,0";
    Require(!kb::scene::ParseRiverPointsText(many, parsed, error) && error.find("32") != std::string::npos, "more than 32 points are refused");
}

void RunRiverSurfaceTest() {
    const kb::scene::RiverComponent river = FallingRiver();
    kb::scene::RiverSurface surface;
    Require(kb::scene::BuildRiverSurface(river, surface), "the river surface builds");
    Require(surface.samples.front().position.x == 0.0F && std::abs(surface.samples.back().position.x - 44.0F) < 0.01F, "the samples run from end to end");
    for (std::size_t index = 1U; index < surface.samples.size(); ++index) {
        Require(surface.samples[index].distance > surface.samples[index - 1U].distance, "the distance along the river grows");
    }
    // The steep drop between the third and the fourth point is one waterfall and nothing else is.
    Require(surface.waterfalls.size() == 1U, "one waterfall is found");
    const kb::scene::RiverWaterfall fall = surface.waterfalls.front();
    Require(std::abs(fall.topHeight - 10.0F) < 0.01F && std::abs(fall.bottomHeight - 2.0F) < 0.01F, "the waterfall drops from 10 to 2");
    Require(std::abs(surface.samples[fall.first].position.x - 20.0F) < 0.01F && std::abs(surface.samples[fall.last].position.x - 24.0F) < 0.5F,
        "the waterfall lies between the points that make the drop");
    Require(!surface.fallVertices.empty() && !surface.streamVertices.empty(), "the stream and the waterfall have their own triangles");
    Require(surface.streamIndices.size() % 3U == 0U && surface.fallIndices.size() % 3U == 0U, "the surfaces are triangles");
    const auto inRange = [](const std::vector<kb::scene::SurfaceGeometryVertex>& vertices, const std::vector<std::uint32_t>& indices) {
        return std::ranges::all_of(indices, [&vertices](std::uint32_t index) { return index < vertices.size(); });
    };
    Require(inRange(surface.streamVertices, surface.streamIndices) && inRange(surface.fallVertices, surface.fallIndices), "every index names a vertex");
    // Budget: a river of 44 units is a few hundred triangles at most.
    Require(surface.streamIndices.size() / 3U + surface.fallIndices.size() / 3U < 400U, "the water has a small triangle budget");
    // The current: a unit normal whose x and z are the velocity over the flow scale, along the flow.
    const kb::scene::SurfaceGeometryVertex middle = surface.streamVertices[1];
    const float length = std::sqrt(middle.normal.x * middle.normal.x + middle.normal.y * middle.normal.y + middle.normal.z * middle.normal.z);
    Require(std::abs(length - 1.0F) < 0.001F && std::abs(middle.normal.x * kb::scene::kRiverFlowScale - 1.5F) < 0.01F && std::abs(middle.normal.z) < 0.001F,
        "the normal carries the current");
    Require(middle.u == 0.5F && surface.streamVertices[0].u == 0.0F && surface.streamVertices[2].u == 1.0F, "u runs across the water");
    // A gentle river has no waterfall; a steeper threshold turns the same drop into a stream.
    kb::scene::RiverComponent gentle = river;
    gentle.points[2].position.y = 9.0F;
    Require(kb::scene::BuildRiverSurface(gentle, surface) && surface.waterfalls.empty() && surface.fallVertices.empty(), "a gentle slope is no waterfall");
    kb::scene::RiverComponent steep = river;
    steep.waterfallSlopeDegrees = 80.0F;
    Require(kb::scene::BuildRiverSurface(steep, surface) && surface.waterfalls.empty(), "a drop under the river's waterfall slope is a stream");
    // A river stays within its sample budget however long it is.
    kb::scene::RiverComponent longRiver{};
    longRiver.pointCount = 2U;
    longRiver.points[1].position = { 100000.0F, 0.0F, 0.0F };
    Require(kb::scene::BuildRiverSurface(longRiver, surface) && surface.samples.size() <= 1024U, "a very long river is sampled more coarsely");
}

} // namespace

void RunTerrainRiverTests() {
    RunChannelProfileTest();
    RunChannelRejectionTest();
    RunChannelOperationTest();
    RunRiverPointsTest();
    RunRiverSurfaceTest();
}
