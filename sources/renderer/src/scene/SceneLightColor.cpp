#include "scene/SceneLightColor.hpp"

#include <algorithm>
#include <cmath>

namespace kb::render {
namespace {

// LIB-141: Tanner Helland's blackbody approximation - the exact same formula as
// RenderMaterialGraphCompiler.cpp's `kbBlackBody` GLSL/HLSL codegen (MAT-50's BlackBody graph
// node), ported to C++ so a light's resolved color can be computed once on the CPU at
// render-sync time instead of requiring a material graph.
[[nodiscard]] std::array<float, 3> BlackBodyRgb(float kelvin) noexcept {
    const float t = std::clamp(kelvin, 1000.0F, 40000.0F) / 100.0F;
    const float r = t <= 66.0F
        ? 1.0F
        : std::clamp(1.29293618606F * std::pow(std::max(t - 60.0F, 0.0001F), -0.1332047592F), 0.0F, 1.0F);
    const float g = t <= 66.0F
        ? std::clamp(0.39008157876F * std::log(std::max(t, 0.0001F)) - 0.63184144378F, 0.0F, 1.0F)
        : std::clamp(1.12989086089F * std::pow(std::max(t - 60.0F, 0.0001F), -0.0755148492F), 0.0F, 1.0F);
    const float b = t >= 66.0F
        ? 1.0F
        : (t <= 19.0F ? 0.0F : std::clamp(0.54320678911F * std::log(std::max(t - 10.0F, 0.0001F)) - 1.19625408914F, 0.0F, 1.0F));
    return { r, g, b };
}

} // namespace

std::array<float, 3> SceneLightColor::Resolve(const kb::scene::LightComponent& light) noexcept {
    if (!light.useColorTemperature) {
        return { light.color.x, light.color.y, light.color.z };
    }
    const std::array<float, 3> blackBody = BlackBodyRgb(light.colorTemperatureKelvin);
    return {
        light.color.x * blackBody[0],
        light.color.y * blackBody[1],
        light.color.z * blackBody[2],
    };
}

} // namespace kb::render
