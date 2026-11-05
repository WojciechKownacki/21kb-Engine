#pragma once

#include "engine/math/EngineMath.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace kb::render {

// World-space triangles. Receiver UV0 must be an atlas without overlapping interiors.
struct LightmapVertex { kb::math::Vec3 position{}, normal{}; float u = 0, v = 0; };
using LightmapTriangle = std::array<LightmapVertex,3>;
struct LightmapLight {
    kb::math::Vec3 position{}, direction{0,-1,0}, color{1,1,1};
    float intensity = 1, range = 10;
    bool directional = false, castsShadow = true;
};
struct StaticLightmapSettings {
    std::uint16_t resolution = 128;
    float ambient = 0.03F, rayBias = 0.001F;
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

} // namespace kb::render
