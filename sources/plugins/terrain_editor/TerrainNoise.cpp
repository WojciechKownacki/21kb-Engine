#include "TerrainNoise.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace kb::terrain_editor {
namespace {

[[nodiscard]] std::uint32_t Hash(std::int32_t x, std::int32_t z, std::uint32_t seed) noexcept {
    std::uint32_t value = static_cast<std::uint32_t>(x) * 0x27D4EB2DU ^ static_cast<std::uint32_t>(z) * 0x165667B1U ^ seed * 0x9E3779B1U;
    value ^= value >> 15U;
    value *= 0x85EBCA6BU;
    value ^= value >> 13U;
    value *= 0xC2B2AE35U;
    value ^= value >> 16U;
    return value;
}

[[nodiscard]] float Fade(float t) noexcept {
    return t * t * t * (t * (t * 6.0F - 15.0F) + 10.0F);
}

// Lattice values in -1..1.
[[nodiscard]] float LatticeValue(std::int32_t x, std::int32_t z, std::uint32_t seed) noexcept {
    return static_cast<float>(Hash(x, z, seed) & 0x00FFFFFFU) / 8388607.5F - 1.0F;
}

constexpr std::array<std::array<float, 2>, 16> kDirections{ {
    { 1.0F, 0.0F }, { 0.92388F, 0.38268F }, { 0.70711F, 0.70711F }, { 0.38268F, 0.92388F },
    { 0.0F, 1.0F }, { -0.38268F, 0.92388F }, { -0.70711F, 0.70711F }, { -0.92388F, 0.38268F },
    { -1.0F, 0.0F }, { -0.92388F, -0.38268F }, { -0.70711F, -0.70711F }, { -0.38268F, -0.92388F },
    { 0.0F, -1.0F }, { 0.38268F, -0.92388F }, { 0.70711F, -0.70711F }, { 0.92388F, -0.38268F },
} };

[[nodiscard]] float LatticeGradient(std::int32_t x, std::int32_t z, std::uint32_t seed, float dx, float dz) noexcept {
    const std::array<float, 2>& direction = kDirections[Hash(x, z, seed) & 15U];
    return direction[0] * dx + direction[1] * dz;
}

[[nodiscard]] float Octave(TerrainNoiseKind kind, float x, float z, std::uint32_t seed) noexcept {
    const float floorX = std::floor(x);
    const float floorZ = std::floor(z);
    const auto ix = static_cast<std::int32_t>(floorX);
    const auto iz = static_cast<std::int32_t>(floorZ);
    const float tx = x - floorX;
    const float tz = z - floorZ;
    const float u = Fade(tx);
    const float v = Fade(tz);
    if (kind == TerrainNoiseKind::Value) {
        return std::lerp(std::lerp(LatticeValue(ix, iz, seed), LatticeValue(ix + 1, iz, seed), u),
            std::lerp(LatticeValue(ix, iz + 1, seed), LatticeValue(ix + 1, iz + 1, seed), u), v);
    }
    // Unit gradients give at most 0.7071 in 2D; scaling by sqrt(2) fills -1..1.
    const float value = std::lerp(
        std::lerp(LatticeGradient(ix, iz, seed, tx, tz), LatticeGradient(ix + 1, iz, seed, tx - 1.0F, tz), u),
        std::lerp(LatticeGradient(ix, iz + 1, seed, tx, tz - 1.0F), LatticeGradient(ix + 1, iz + 1, seed, tx - 1.0F, tz - 1.0F), u), v);
    return std::clamp(value * 1.41421356F, -1.0F, 1.0F);
}

} // namespace

bool IsTerrainNoiseValid(const TerrainNoiseSettings& settings) noexcept {
    return static_cast<std::uint8_t>(settings.kind) <= static_cast<std::uint8_t>(TerrainNoiseKind::Value) &&
        std::isfinite(settings.scale) && settings.scale >= 0.01F && settings.scale <= 1'000'000.0F &&
        settings.octaves >= 1U && settings.octaves <= kTerrainNoiseMaximumOctaves &&
        std::isfinite(settings.lacunarity) && settings.lacunarity >= 1.0F && settings.lacunarity <= 8.0F &&
        std::isfinite(settings.gain) && settings.gain > 0.0F && settings.gain <= 1.0F;
}

float SampleTerrainNoise(const TerrainNoiseSettings& settings, float x, float z) noexcept {
    float frequency = 1.0F / settings.scale;
    float amplitude = 1.0F;
    float sum = 0.0F;
    float total = 0.0F;
    for (std::uint8_t octave = 0U; octave < settings.octaves; ++octave) {
        sum += amplitude * Octave(settings.kind, x * frequency, z * frequency, settings.seed + octave * 0x9E3779B1U);
        total += amplitude;
        frequency *= settings.lacunarity;
        amplitude *= settings.gain;
    }
    return sum / total;
}

float SampleTerrainRidgedNoise(const TerrainNoiseSettings& settings, float x, float z) noexcept {
    float frequency = 1.0F / settings.scale;
    float amplitude = 1.0F;
    float weight = 1.0F;
    float sum = 0.0F;
    float total = 0.0F;
    for (std::uint8_t octave = 0U; octave < settings.octaves; ++octave) {
        float signal = 1.0F - std::abs(Octave(settings.kind, x * frequency, z * frequency, settings.seed + octave * 0x9E3779B1U));
        signal *= signal * weight;
        weight = std::clamp(signal * 2.0F, 0.0F, 1.0F);
        sum += signal * amplitude;
        total += amplitude;
        frequency *= settings.lacunarity;
        amplitude *= settings.gain;
    }
    return sum / total;
}

std::string_view TerrainNoiseKindName(TerrainNoiseKind kind) noexcept {
    return kind == TerrainNoiseKind::Value ? "value" : "gradient";
}

std::optional<TerrainNoiseKind> ParseTerrainNoiseKind(std::string_view text) noexcept {
    if (text == "gradient") return TerrainNoiseKind::Gradient;
    if (text == "value") return TerrainNoiseKind::Value;
    return std::nullopt;
}

} // namespace kb::terrain_editor
