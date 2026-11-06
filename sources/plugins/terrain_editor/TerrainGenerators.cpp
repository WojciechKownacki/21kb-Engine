#include "TerrainGenerators.hpp"

#include "TerrainParallel.hpp"

#include <algorithm>
#include <cmath>

namespace kb::terrain_editor {
namespace {

[[nodiscard]] float SmoothStep(float value) noexcept {
    value = std::clamp(value, 0.0F, 1.0F);
    return value * value * (3.0F - 2.0F * value);
}

struct GeneratorName { std::string_view name; TerrainGeneratorKind kind; };
constexpr GeneratorName kGeneratorNames[]{ { "fbm", TerrainGeneratorKind::Fbm }, { "ridged", TerrainGeneratorKind::Ridged },
    { "island", TerrainGeneratorKind::Island }, { "plateau", TerrainGeneratorKind::Plateau },
    { "valley", TerrainGeneratorKind::Valley } };

} // namespace

std::string_view TerrainGeneratorKindName(TerrainGeneratorKind kind) noexcept {
    for (const GeneratorName& entry : kGeneratorNames) if (entry.kind == kind) return entry.name;
    return {};
}

std::optional<TerrainGeneratorKind> ParseTerrainGeneratorKind(std::string_view text) noexcept {
    for (const GeneratorName& entry : kGeneratorNames) if (entry.name == text) return entry.kind;
    return std::nullopt;
}

bool IsTerrainGeneratorValid(const TerrainGeneratorSettings& settings) noexcept {
    return static_cast<std::uint8_t>(settings.kind) <= static_cast<std::uint8_t>(TerrainGeneratorKind::Valley) &&
        std::isfinite(settings.amplitude) && std::abs(settings.amplitude) <= 100'000.0F &&
        std::isfinite(settings.baseHeight) && std::abs(settings.baseHeight) <= 100'000.0F &&
        IsTerrainNoiseValid(TerrainNoiseSettings{ .kind = settings.noiseKind, .scale = settings.scale, .octaves = settings.octaves,
            .lacunarity = settings.lacunarity, .gain = settings.gain }) &&
        std::isfinite(settings.warp) && settings.warp >= 0.0F && settings.warp <= 100'000.0F &&
        std::isfinite(settings.islandRadius) && settings.islandRadius > 0.0F && settings.islandRadius <= 2.0F &&
        std::isfinite(settings.islandFalloff) && settings.islandFalloff > 0.0F && settings.islandFalloff <= 2.0F &&
        std::isfinite(settings.plateauLevel) && settings.plateauLevel > 0.0F && settings.plateauLevel < 1.0F &&
        std::isfinite(settings.plateauSoftness) && settings.plateauSoftness > 0.0F && settings.plateauSoftness <= 0.5F &&
        std::isfinite(settings.valleyWidth) && settings.valleyWidth >= 0.0F && settings.valleyWidth < 1.0F &&
        std::isfinite(settings.valleyWall) && settings.valleyWall > 0.0F && settings.valleyWall <= 2.0F;
}

bool GenerateTerrain(
    kb::assets::TerrainAsset& terrain, const TerrainGeneratorSettings& settings, TerrainTaskControl& control, std::string& error) {
    if (!kb::assets::IsTerrainAssetValid(terrain, &error)) return false;
    if (!IsTerrainGeneratorValid(settings)) {
        error = "generator settings are out of range (scale >= 0.01, octaves 1..8, lacunarity 1..8, gain 0..1, warp >= 0, "
                "island radius and falloff > 0, plateau level 0..1, valley width 0..1 and wall > 0)";
        return false;
    }
    const TerrainNoiseSettings noise{ .kind = settings.noiseKind, .scale = settings.scale, .octaves = settings.octaves,
        .lacunarity = settings.lacunarity, .gain = settings.gain, .seed = settings.seed };
    TerrainNoiseSettings warpX = noise;
    warpX.seed = settings.seed ^ 0x5BD1E995U;
    warpX.octaves = std::min<std::uint8_t>(noise.octaves, 3U);
    TerrainNoiseSettings warpZ = warpX;
    warpZ.seed = settings.seed ^ 0x1B873593U;

    const float halfX = terrain.worldSizeX * 0.5F;
    const float halfZ = terrain.worldSizeZ * 0.5F;
    const float stepX = terrain.worldSizeX / static_cast<float>(terrain.width - 1U);
    const float stepZ = terrain.worldSizeZ / static_cast<float>(terrain.height - 1U);
    control.Begin(terrain.height);
    ParallelForRange(0U, terrain.height, std::max(1U, 16'384U / terrain.width), [&](std::uint32_t firstRow, std::uint32_t endRow, std::uint32_t) {
        for (std::uint32_t row = firstRow; row < endRow; ++row) {
            if (control.Cancelled()) return;
            const float z = static_cast<float>(row) * stepZ - halfZ;
            float* heights = terrain.heights.data() + static_cast<std::size_t>(row) * terrain.width;
            for (std::uint32_t column = 0U; column < terrain.width; ++column) {
                const float x = static_cast<float>(column) * stepX - halfX;
                float sampleX = x;
                float sampleZ = z;
                if (settings.warp > 0.0F) {
                    sampleX += settings.warp * SampleTerrainNoise(warpX, x, z);
                    sampleZ += settings.warp * SampleTerrainNoise(warpZ, x, z);
                }
                float shape = 0.0F;
                switch (settings.kind) {
                case TerrainGeneratorKind::Fbm:
                    shape = 0.5F + 0.5F * SampleTerrainNoise(noise, sampleX, sampleZ);
                    break;
                case TerrainGeneratorKind::Ridged:
                    shape = SampleTerrainRidgedNoise(noise, sampleX, sampleZ);
                    break;
                case TerrainGeneratorKind::Island: {
                    const float distance = std::sqrt((x / halfX) * (x / halfX) + (z / halfZ) * (z / halfZ));
                    const float land = 1.0F - SmoothStep((distance - settings.islandRadius) / settings.islandFalloff);
                    const float relief = 0.25F + 0.75F * (0.5F + 0.5F * SampleTerrainNoise(noise, sampleX, sampleZ));
                    shape = land * relief - 0.2F * (1.0F - land);
                    break;
                }
                case TerrainGeneratorKind::Plateau: {
                    const float raw = 0.5F + 0.5F * SampleTerrainNoise(noise, sampleX, sampleZ);
                    const float mesa = SmoothStep((raw - (settings.plateauLevel - settings.plateauSoftness)) / (2.0F * settings.plateauSoftness));
                    shape = std::lerp(0.35F * raw, 1.0F + 0.05F * (raw - settings.plateauLevel), mesa);
                    break;
                }
                case TerrainGeneratorKind::Valley: {
                    // The warped x makes the floor wind; the walls rise from its edge to the side of the terrain.
                    const float raw = 0.5F + 0.5F * SampleTerrainNoise(noise, sampleX, sampleZ);
                    const float wall = SmoothStep((std::abs(sampleX) / halfX - settings.valleyWidth) / settings.valleyWall);
                    shape = std::lerp(0.12F * raw, 0.35F + 0.65F * raw, wall);
                    break;
                }
                }
                const float height = settings.baseHeight + settings.amplitude * shape;
                heights[column] = std::clamp(settings.additive ? heights[column] + height : height, -1'000'000.0F, 1'000'000.0F);
            }
            control.Advance(1U);
        }
    }, control.maxWorkers);
    if (control.Cancelled()) {
        error = "cancelled";
        return false;
    }
    return true;
}

} // namespace kb::terrain_editor
