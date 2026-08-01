#include "TerrainBrush.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>

namespace kb::terrain_editor {
namespace {

[[nodiscard]] std::size_t VertexIndex(const kb::assets::TerrainAsset& terrain, std::uint32_t x, std::uint32_t z) noexcept {
    return static_cast<std::size_t>(z) * terrain.width + x;
}

[[nodiscard]] std::size_t CellIndex(const kb::assets::TerrainAsset& terrain, std::uint32_t x, std::uint32_t z) noexcept {
    return static_cast<std::size_t>(z) * (terrain.width - 1U) + x;
}

[[nodiscard]] float SmoothStep(float value) noexcept {
    value = std::clamp(value, 0.0F, 1.0F);
    return value * value * (3.0F - 2.0F * value);
}

[[nodiscard]] float HashNoise(std::uint32_t x, std::uint32_t z, std::uint32_t seed) noexcept {
    std::uint32_t value = x * 0x9E3779B9U ^ z * 0x85EBCA6BU ^ seed * 0xC2B2AE35U;
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return (static_cast<float>(value & 0x00FFFFFFU) / 8388607.5F) - 1.0F;
}

[[nodiscard]] float Weight(
    float distance,
    std::uint32_t sampleX,
    std::uint32_t sampleZ,
    const TerrainBrushSettings& settings) noexcept {
    const float normalized = std::clamp(distance / settings.radius, 0.0F, 1.0F);
    const auto softRound = [&settings, normalized] {
        if (normalized <= settings.falloff) return 1.0F;
        const float edgeWidth = std::max(1.0F - settings.falloff, 0.0001F);
        return 1.0F - SmoothStep((normalized - settings.falloff) / edgeWidth);
    };
    switch (settings.shape) {
    case TerrainBrushShape::SoftRound:
        return softRound();
    case TerrainBrushShape::HardRound:
        return normalized < 0.96F ? 1.0F : 1.0F - SmoothStep((normalized - 0.96F) / 0.04F);
    case TerrainBrushShape::LinearRound:
        return 1.0F - normalized;
    case TerrainBrushShape::Bell: {
        constexpr float kPi = 3.14159265358979323846F;
        return 0.5F + 0.5F * std::cos(normalized * kPi);
    }
    case TerrainBrushShape::Ring: {
        const float ringDistance = std::abs(normalized - 0.58F) / 0.24F;
        return 1.0F - SmoothStep(ringDistance);
    }
    case TerrainBrushShape::Speckle:
        return HashNoise(sampleX, sampleZ, settings.noiseSeed ^ 0xA511E9B3U) > -0.18F
            ? softRound()
            : 0.0F;
    }
    return softRound();
}

void MarkChanged(TerrainBrushResult& result, std::uint32_t x, std::uint32_t z) noexcept {
    ++result.changedSamples;
    result.minX = std::min(result.minX, x);
    result.minZ = std::min(result.minZ, z);
    result.maxX = std::max(result.maxX, x);
    result.maxZ = std::max(result.maxZ, z);
}

[[nodiscard]] float SmoothedHeight(
    const kb::assets::TerrainAsset& terrain,
    std::span<const float> sourceHeights,
    std::uint32_t x,
    std::uint32_t z) noexcept {
    float sum = 0.0F;
    std::uint32_t count = 0U;
    const std::uint32_t minX = x == 0U ? 0U : x - 1U;
    const std::uint32_t minZ = z == 0U ? 0U : z - 1U;
    const std::uint32_t maxX = std::min(x + 1U, terrain.width - 1U);
    const std::uint32_t maxZ = std::min(z + 1U, terrain.height - 1U);
    for (std::uint32_t sampleZ = minZ; sampleZ <= maxZ; ++sampleZ) {
        for (std::uint32_t sampleX = minX; sampleX <= maxX; ++sampleX) {
            sum += sourceHeights[VertexIndex(terrain, sampleX, sampleZ)];
            ++count;
        }
    }
    return sum / static_cast<float>(count);
}

} // namespace

bool IsTerrainBrushSettingsValid(const TerrainBrushSettings& settings) noexcept {
    return static_cast<std::uint8_t>(settings.shape) <= static_cast<std::uint8_t>(TerrainBrushShape::Speckle) &&
        std::isfinite(settings.radius) && settings.radius > 0.0F && settings.radius <= 100'000.0F &&
        std::isfinite(settings.strength) && settings.strength >= 0.0F && settings.strength <= 100'000.0F &&
        std::isfinite(settings.falloff) && settings.falloff >= 0.0F && settings.falloff <= 1.0F &&
        std::isfinite(settings.targetHeight) && std::abs(settings.targetHeight) <= 1'000'000.0F &&
        std::isfinite(settings.terraceStep) && settings.terraceStep > 0.0F && settings.terraceStep <= 100'000.0F;
}

TerrainBrushResult ApplyTerrainBrush(
    kb::assets::TerrainAsset& terrain,
    const TerrainBrushSettings& settings,
    const TerrainBrushStamp& stamp) noexcept {
    TerrainBrushResult result{};
    if (!kb::assets::IsTerrainAssetValid(terrain) || !IsTerrainBrushSettingsValid(settings) ||
        !std::isfinite(stamp.localX) || !std::isfinite(stamp.localZ) || !std::isfinite(stamp.pressure)) return result;

    const float pressure = std::clamp(stamp.pressure, 0.0F, 1.0F);
    const float cellSizeX = terrain.worldSizeX / static_cast<float>(terrain.width - 1U);
    const float cellSizeZ = terrain.worldSizeZ / static_cast<float>(terrain.height - 1U);
    const float centerSampleX = (stamp.localX + terrain.worldSizeX * 0.5F) / cellSizeX;
    const float centerSampleZ = (stamp.localZ + terrain.worldSizeZ * 0.5F) / cellSizeZ;
    const int radiusX = static_cast<int>(std::ceil(settings.radius / cellSizeX));
    const int radiusZ = static_cast<int>(std::ceil(settings.radius / cellSizeZ));
    const int minX = std::max(0, static_cast<int>(std::floor(centerSampleX)) - radiusX);
    const int minZ = std::max(0, static_cast<int>(std::floor(centerSampleZ)) - radiusZ);
    const int maxX = std::min(static_cast<int>(terrain.width) - 1, static_cast<int>(std::ceil(centerSampleX)) + radiusX);
    const int maxZ = std::min(static_cast<int>(terrain.height) - 1, static_cast<int>(std::ceil(centerSampleZ)) + radiusZ);

    if (settings.mode == TerrainBrushMode::CutHole || settings.mode == TerrainBrushMode::FillHole) {
        const std::uint8_t target = settings.mode == TerrainBrushMode::CutHole ? 1U : 0U;
        for (int z = minZ; z < maxZ; ++z) {
            for (int x = minX; x < maxX; ++x) {
                const float worldX = (static_cast<float>(x) + 0.5F) * cellSizeX - terrain.worldSizeX * 0.5F;
                const float worldZ = (static_cast<float>(z) + 0.5F) * cellSizeZ - terrain.worldSizeZ * 0.5F;
                const float distance = std::hypot(worldX - stamp.localX, worldZ - stamp.localZ);
                if (distance > settings.radius ||
                    Weight(distance, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z), settings) <= 0.25F) continue;
                std::uint8_t& hole = terrain.holes[CellIndex(terrain, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z))];
                if (hole != target) {
                    hole = target;
                    MarkChanged(result, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z));
                }
            }
        }
        return result;
    }

    const std::vector<float> sourceHeights =
        settings.mode == TerrainBrushMode::Smooth
            ? terrain.heights
            : std::vector<float>{};
    for (int z = minZ; z <= maxZ; ++z) {
        for (int x = minX; x <= maxX; ++x) {
            const float worldX = static_cast<float>(x) * cellSizeX - terrain.worldSizeX * 0.5F;
            const float worldZ = static_cast<float>(z) * cellSizeZ - terrain.worldSizeZ * 0.5F;
            const float distance = std::hypot(worldX - stamp.localX, worldZ - stamp.localZ);
            if (distance > settings.radius) continue;
            const float influence = Weight(
                distance,
                static_cast<std::uint32_t>(x),
                static_cast<std::uint32_t>(z),
                settings) * pressure;
            const std::size_t index = VertexIndex(terrain, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z));
            const float before = terrain.heights[index];
            float after = before;
            switch (settings.mode) {
            case TerrainBrushMode::Raise: after += settings.strength * influence; break;
            case TerrainBrushMode::Lower: after -= settings.strength * influence; break;
            case TerrainBrushMode::Smooth:
                after = std::lerp(before, SmoothedHeight(terrain, sourceHeights, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z)), std::clamp(settings.strength * influence, 0.0F, 1.0F));
                break;
            case TerrainBrushMode::Flatten:
                after = std::lerp(before, settings.targetHeight, std::clamp(settings.strength * influence, 0.0F, 1.0F));
                break;
            case TerrainBrushMode::Noise:
                after += HashNoise(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z), settings.noiseSeed) * settings.strength * influence;
                break;
            case TerrainBrushMode::Terrace: {
                const float target = std::round(before / settings.terraceStep) * settings.terraceStep;
                after = std::lerp(before, target, std::clamp(settings.strength * influence, 0.0F, 1.0F));
                break;
            }
            case TerrainBrushMode::CutHole:
            case TerrainBrushMode::FillHole:
                break;
            }
            after = std::clamp(after, -1'000'000.0F, 1'000'000.0F);
            if (std::abs(after - before) > std::numeric_limits<float>::epsilon()) {
                terrain.heights[index] = after;
                MarkChanged(result, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z));
            }
        }
    }
    return result;
}

} // namespace kb::terrain_editor
