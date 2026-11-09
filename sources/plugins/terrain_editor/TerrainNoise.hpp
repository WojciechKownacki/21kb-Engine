#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace kb::terrain_editor {

enum class TerrainNoiseKind : std::uint8_t {
    Gradient, // smooth ridges and valleys without grid artefacts (Perlin style)
    Value,    // blobbier, softer lumps
};

// Coherent noise of the plane: the same point always has the same value, and nearby points have
// nearby values, so a stroke's bumps join up with the strokes beside it. Several octaves add finer
// detail at lower weight. Values lie in -1..1.
struct TerrainNoiseSettings {
    TerrainNoiseKind kind = TerrainNoiseKind::Gradient;
    float scale = 16.0F; // world units of the largest feature
    std::uint8_t octaves = 3U;
    float lacunarity = 2.0F; // frequency factor per octave
    float gain = 0.5F;       // weight factor per octave
    std::uint32_t seed = 1U;
};

inline constexpr std::uint8_t kTerrainNoiseMaximumOctaves = 8U;

[[nodiscard]] bool IsTerrainNoiseValid(const TerrainNoiseSettings& settings) noexcept;
[[nodiscard]] float SampleTerrainNoise(const TerrainNoiseSettings& settings, float x, float z) noexcept;
// Ridged noise, 0..1: sharp crests where the noise crosses zero, each octave weighted by the one
// before so crests carry the detail. The shape of mountain ranges.
[[nodiscard]] float SampleTerrainRidgedNoise(const TerrainNoiseSettings& settings, float x, float z) noexcept;

[[nodiscard]] std::string_view TerrainNoiseKindName(TerrainNoiseKind kind) noexcept;
[[nodiscard]] std::optional<TerrainNoiseKind> ParseTerrainNoiseKind(std::string_view text) noexcept;

} // namespace kb::terrain_editor
