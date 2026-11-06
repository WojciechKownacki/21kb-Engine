#include "TerrainChannel.hpp"

#include "TerrainParallel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace kb::terrain_editor {
namespace {

// Below this many samples a carve runs on the calling thread.
constexpr std::uint32_t kParallelSamples = 24'576U;

[[nodiscard]] float SmoothStep(float value) noexcept {
    value = std::clamp(value, 0.0F, 1.0F);
    return value * value * (3.0F - 2.0F * value);
}

} // namespace

TerrainSegmentProbe ProbeTerrainSegment(float ax, float az, float bx, float bz, float x, float z) noexcept {
    const float abx = bx - ax;
    const float abz = bz - az;
    const float lengthSquared = abx * abx + abz * abz;
    const float dx = x - ax;
    const float dz = z - az;
    const float along = lengthSquared > 0.0F ? std::clamp((dx * abx + dz * abz) / lengthSquared, 0.0F, 1.0F) : 0.0F;
    const float offsetX = dx - abx * along;
    const float offsetZ = dz - abz * along;
    return { along, std::sqrt(offsetX * offsetX + offsetZ * offsetZ) };
}

float TerrainChannelEdgeWeight(float distance, float flatDistance, float outerDistance) noexcept {
    if (distance <= flatDistance) return 1.0F;
    if (distance >= outerDistance) return 0.0F;
    return 1.0F - SmoothStep((distance - flatDistance) / std::max(outerDistance - flatDistance, 0.0001F));
}

TerrainChannelProbe ProbeTerrainChannel(std::span<const TerrainChannelNode> nodes, float x, float z) noexcept {
    TerrainChannelProbe best{ .distance = std::numeric_limits<float>::max() };
    if (nodes.size() == 1U) {
        return { std::hypot(x - nodes[0].x, z - nodes[0].z), nodes[0].level, nodes[0].halfWidth, nodes[0].depth };
    }
    for (std::size_t index = 0U; index + 1U < nodes.size(); ++index) {
        const TerrainChannelNode& a = nodes[index];
        const TerrainChannelNode& b = nodes[index + 1U];
        const TerrainSegmentProbe probe = ProbeTerrainSegment(a.x, a.z, b.x, b.z, x, z);
        if (probe.distance >= best.distance) continue;
        best = { probe.distance, std::lerp(a.level, b.level, probe.along), std::lerp(a.halfWidth, b.halfWidth, probe.along),
            std::lerp(a.depth, b.depth, probe.along) };
    }
    return best;
}

bool IsTerrainChannelValid(std::span<const TerrainChannelNode> nodes, const TerrainChannelSettings& settings) noexcept {
    if (nodes.size() < 2U || !std::isfinite(settings.bankWidth) || settings.bankWidth < 0.0F || settings.bankWidth > 100'000.0F) return false;
    return std::ranges::all_of(nodes, [](const TerrainChannelNode& node) {
        return std::isfinite(node.x) && std::isfinite(node.z) && std::isfinite(node.level) && std::abs(node.level) <= 1'000'000.0F &&
            std::isfinite(node.halfWidth) && node.halfWidth > 0.0F && node.halfWidth <= 100'000.0F &&
            std::isfinite(node.depth) && node.depth >= 0.0F && node.depth <= 100'000.0F;
    });
}

float TerrainChannelHeight(const TerrainChannelProbe& probe, const TerrainChannelSettings& settings, float& weight) noexcept {
    if (probe.distance <= probe.halfWidth) {
        const float across = probe.distance / probe.halfWidth;
        weight = 1.0F;
        return probe.level - probe.depth * (1.0F - across * across);
    }
    weight = TerrainChannelEdgeWeight(probe.distance, probe.halfWidth, probe.halfWidth + settings.bankWidth);
    return probe.level;
}

std::vector<TerrainChannelNode> RiverChannelNodes(const kb::scene::RiverComponent& river, const kb::scene::RiverPlacement& riverPlacement,
    const kb::scene::RiverPlacement& terrainPlacement, float step) {
    constexpr std::uint32_t kMaxNodes = 4096U;
    std::vector<kb::scene::RiverSample> samples;
    std::vector<TerrainChannelNode> nodes;
    if (!kb::scene::SampleRiverInFrame(river, riverPlacement, terrainPlacement, step, kMaxNodes, samples)) return nodes;
    nodes.reserve(samples.size());
    for (const kb::scene::RiverSample& sample : samples) {
        nodes.push_back({ sample.position.x, sample.position.z, sample.position.y, sample.width * 0.5F, sample.depth });
    }
    return nodes;
}

TerrainBrushResult CarveTerrainChannel(
    kb::assets::TerrainAsset& terrain, std::span<const TerrainChannelNode> nodes, const TerrainChannelSettings& settings) noexcept {
    if (!IsTerrainChannelValid(nodes, settings) || terrain.width < 2U || terrain.height < 2U ||
        terrain.heights.size() != static_cast<std::size_t>(terrain.width) * terrain.height) {
        return {};
    }
    const float cellX = terrain.worldSizeX / static_cast<float>(terrain.width - 1U);
    const float cellZ = terrain.worldSizeZ / static_cast<float>(terrain.height - 1U);
    const float halfX = terrain.worldSizeX * 0.5F;
    const float halfZ = terrain.worldSizeZ * 0.5F;
    float lowX = std::numeric_limits<float>::max();
    float lowZ = lowX;
    float highX = -lowX;
    float highZ = -lowX;
    float reach = 0.0F;
    for (const TerrainChannelNode& node : nodes) {
        lowX = std::min(lowX, node.x);
        lowZ = std::min(lowZ, node.z);
        highX = std::max(highX, node.x);
        highZ = std::max(highZ, node.z);
        reach = std::max(reach, node.halfWidth + settings.bankWidth);
    }
    const float firstX = (lowX - reach + halfX) / cellX;
    const float firstZ = (lowZ - reach + halfZ) / cellZ;
    const float lastX = (highX + reach + halfX) / cellX;
    const float lastZ = (highZ + reach + halfZ) / cellZ;
    const float limitX = static_cast<float>(terrain.width - 1U);
    const float limitZ = static_cast<float>(terrain.height - 1U);
    if (lastX < 0.0F || lastZ < 0.0F || firstX > limitX || firstZ > limitZ) return {};
    const auto minX = static_cast<std::uint32_t>(std::floor(std::max(firstX, 0.0F)));
    const auto minZ = static_cast<std::uint32_t>(std::floor(std::max(firstZ, 0.0F)));
    const auto maxX = static_cast<std::uint32_t>(std::ceil(std::min(lastX, limitX)));
    const auto maxZ = static_cast<std::uint32_t>(std::ceil(std::min(lastZ, limitZ)));
    const std::uint32_t rowWidth = maxX - minX + 1U;
    std::array<TerrainBrushResult, kTerrainMaximumWorkers> partial{};
    ParallelForRange(minZ, maxZ + 1U, std::max(1U, kParallelSamples / rowWidth), [&](std::uint32_t firstRow, std::uint32_t endRow, std::uint32_t part) noexcept {
        TerrainBrushResult& local = partial[part];
        for (std::uint32_t z = firstRow; z < endRow; ++z) {
            const float worldZ = static_cast<float>(z) * cellZ - halfZ;
            for (std::uint32_t x = minX; x <= maxX; ++x) {
                const float worldX = static_cast<float>(x) * cellX - halfX;
                const TerrainChannelProbe probe = ProbeTerrainChannel(nodes, worldX, worldZ);
                if (probe.distance > probe.halfWidth + settings.bankWidth) continue;
                float weight = 0.0F;
                const float target = TerrainChannelHeight(probe, settings, weight);
                float& height = terrain.heights[static_cast<std::size_t>(z) * terrain.width + x];
                const float after = std::lerp(height, target, weight);
                if (std::abs(after - height) <= std::numeric_limits<float>::epsilon()) continue;
                height = after;
                ++local.changedSamples;
                local.minX = std::min(local.minX, x);
                local.minZ = std::min(local.minZ, z);
                local.maxX = std::max(local.maxX, x);
                local.maxZ = std::max(local.maxZ, z);
            }
        }
    });
    TerrainBrushResult result{};
    for (const TerrainBrushResult& local : partial) {
        result.changedSamples += local.changedSamples;
        result.minX = std::min(result.minX, local.minX);
        result.minZ = std::min(result.minZ, local.minZ);
        result.maxX = std::max(result.maxX, local.maxX);
        result.maxZ = std::max(result.maxZ, local.maxZ);
    }
    return result;
}

} // namespace kb::terrain_editor
