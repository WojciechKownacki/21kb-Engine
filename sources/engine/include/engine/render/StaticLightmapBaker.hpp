#pragma once

#include "engine/math/EngineMath.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
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

} // namespace kb::render
