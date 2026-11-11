#pragma once

#include "engine/math/EngineMath.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::render {

// World-space triangles. Receiver UV0 must be an atlas without overlapping interiors.
// albedo only matters for bounced light; the triangle uses the mean of its corners.
struct LightmapVertex { kb::math::Vec3 position{}, normal{}; float u = 0, v = 0; kb::math::Vec3 albedo{0.5F,0.5F,0.5F}; };
using LightmapTriangle = std::array<LightmapVertex,3>;
struct LightmapLight {
    kb::math::Vec3 position{}, direction{0,-1,0}, color{1,1,1};
    float intensity = 1, range = 10;
    bool directional = false, castsShadow = true;
    // A spot is a point light inside a cone around `direction`: full strength inside the inner half angle,
    // falling off with the square of the linear ramp to the outer half angle (the runtime light model).
    bool spot = false;
    float innerConeDegrees = 25, outerConeDegrees = 35;
};
struct StaticLightmapSettings {
    std::uint16_t resolution = 128;
    float ambient = 0.03F, rayBias = 0.001F;
    // Diffuse bounces after the direct pass (0..4); 0 gives the direct-only result.
    std::uint8_t bounces = 1;
};
struct StaticLightmapResult {
    std::string error;
    // sRGB diffuse irradiance, top-left image origin, ready for an emissive texture slot.
    std::vector<std::uint8_t> rgba;
    std::uint32_t coveredTexels = 0, shadowedSamples = 0;
    [[nodiscard]] bool Succeeded() const noexcept { return error.empty() && coveredTexels != 0; }
};

// Deterministic CPU direct-light bake, including hard ray-traced shadows. No GPU or simulation.
// Returns an error for invalid data, excessive work or UV overlap; never publishes partial bytes.
[[nodiscard]] StaticLightmapResult BakeStaticLightmap(std::span<const LightmapTriangle> receiver,
    std::span<const LightmapTriangle> occluders, std::span<const LightmapLight> lights,
    const StaticLightmapSettings& settings = {});

// Charts a triangle soup into a non-overlapping atlas (uv holds u,v for each of the three corners per triangle,
// so the mesh must be unindexed). Deterministic; charts keep a gap that survives the bake's texel padding.
struct LightmapUvLayout {
    std::string error;
    std::vector<float> uv;
    [[nodiscard]] bool Succeeded() const noexcept { return error.empty() && !uv.empty(); }
};
[[nodiscard]] LightmapUvLayout GenerateLightmapUv(std::span<const LightmapTriangle> triangles, std::uint16_t resolution = 128);

// True when the triangles' u,v are inside [0,1], non-degenerate and do not overlap at this resolution.
[[nodiscard]] bool HasUsableLightmapUv(std::span<const LightmapTriangle> triangles, std::uint16_t resolution);

// Second-order spherical harmonics of incoming light: nine RGB coefficients in the usual band order
// (L00, L1-1, L10, L11, L2-2, L2-1, L20, L21, L22), with Y1-1 ~ y, Y10 ~ z, Y11 ~ x.
struct ShL2 { std::array<kb::math::Vec3,9> c{}; };
// Adds the projection of light arriving from `fromDirection` (unit, towards the light) with weight `radiance`.
void AddShL2Direction(ShL2& sh, kb::math::Vec3 fromDirection, kb::math::Vec3 radiance);
// Cosine-convolved diffuse term for a surface normal, in the lightmap's irradiance units (a light of
// intensity 1 straight onto the surface gives 1), so albedo * result is the lit colour.
[[nodiscard]] kb::math::Vec3 EvaluateShL2Irradiance(const ShL2& sh, kb::math::Vec3 normal);

// A baked receiver as the probe pass sees it: the lightmap-space triangles and their finished lightmap.
struct BakedLightmapSurface {
    std::span<const LightmapTriangle> triangles;
    std::span<const std::uint8_t> rgba;
    std::uint16_t resolution = 0;
};
struct LightProbeSettings {
    float spacing = 2.0F;
    std::uint32_t maxProbes = 4096;
    float ambient = 0.03F;
};
// A regular grid of probes over [origin, origin + (dimensions - 1) * spacing]; x is the fastest index.
struct LightProbeGrid {
    std::string error;
    kb::math::Vec3 origin{};
    float spacing = 0;
    std::array<std::uint32_t,3> dimensions{};
    std::vector<ShL2> probes;
    std::uint32_t invalidProbes = 0;
    [[nodiscard]] bool Succeeded() const noexcept { return error.empty() && !probes.empty(); }
};
// Traces the static geometry with the lightmap tracer: each probe gathers the outgoing light (albedo * baked
// irradiance, so every bounce the lightmaps hold) of what its rays hit. Light that arrives straight from a light
// is not stored: those lights still shine on moving objects in real time, so a probe holds indirect light only.
// Probes inside geometry are refilled from valid neighbours. Deterministic; never publishes partial data.
[[nodiscard]] LightProbeGrid BakeLightProbes(std::span<const BakedLightmapSurface> surfaces,
    kb::math::Vec3 boundsMin, kb::math::Vec3 boundsMax, const LightProbeSettings& settings = {});
// Trilinear SH at a world position (clamped to the grid).
[[nodiscard]] ShL2 SampleLightProbeGrid(const LightProbeGrid& grid, kb::math::Vec3 position);
// Small little-endian binary form: "KBLP", version, origin, spacing, dimensions, 27 floats per probe.
[[nodiscard]] std::string SerializeLightProbeGrid(const LightProbeGrid& grid);
[[nodiscard]] LightProbeGrid ParseLightProbeGrid(std::string_view bytes);

inline constexpr std::string_view kLightProbeGridAssetType = "LightProbeGrid";
inline constexpr std::string_view kLightProbeGridExtension = ".kblp";
// The file-safe form of a name in baked lighting output: anything but letters, digits, '_' and '-' becomes '_'.
[[nodiscard]] std::string BakedLightingName(std::string_view text);
// The folder a scene's lighting bake writes to and the probe grid inside it, from the scene file's stem.
[[nodiscard]] std::string BakedLightingFolder(std::string_view sceneName);
[[nodiscard]] std::string LightProbeGridVirtualPath(std::string_view sceneName);

} // namespace kb::render
