#pragma once

#include "engine/scene/LightComponent.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace kb::editor {

// One click sets a light up for a common use: its type, colour temperature, brightness and reach. The brightness is the
// light's own intensity, on the same scale the Intensity row shows (1 = a light that returns a surface's colour unchanged).
enum class LightPreset : int {
    Sun,
    OvercastSky,
    RoomBulb,
    Torch,
    StreetLamp,
    Spotlight,
};
inline constexpr int kLightPresetCount = 6;

inline void ApplyLightPreset(kb::scene::LightComponent& light, LightPreset preset) noexcept {
    using Kind = kb::scene::LightKind;
    struct Values {
        Kind kind;
        float kelvin;
        float intensity;
        float range;  // 0 = unchanged
        float inner;  // spot cone, degrees; 0 = unchanged
        float outer;
    };
    constexpr Values kValues[kLightPresetCount]{
        { Kind::Directional, 5600.0F, 3.125F, 0.0F, 0.0F, 0.0F },     // sun
        { Kind::Directional, 7000.0F, 0.46875F, 0.0F, 0.0F, 0.0F },   // overcast sky
        { Kind::Point, 2700.0F, 6.4F, 8.0F, 0.0F, 0.0F },             // room bulb
        { Kind::Point, 1900.0F, 3.0F, 6.0F, 0.0F, 0.0F },             // torch or candle
        { Kind::Point, 4000.0F, 30.0F, 20.0F, 0.0F, 0.0F },           // street lamp
        { Kind::Spot, 3000.0F, 25.0F, 15.0F, 20.0F, 35.0F },          // spotlight
    };
    const Values& values = kValues[static_cast<int>(preset)];
    light.kind = values.kind;
    light.color = kb::scene::Vec3{ 1.0F, 1.0F, 1.0F };
    light.useColorTemperature = true;
    light.colorTemperatureKelvin = values.kelvin;
    light.intensity = values.intensity;
    if (values.range > 0.0F) {
        light.range = values.range;
    }
    if (values.outer > 0.0F) {
        light.innerConeDegrees = values.inner;
        light.outerConeDegrees = values.outer;
    }
}

// The preset a light amounts to, nullopt ("Custom") once any value it sets was changed.
[[nodiscard]] inline std::optional<LightPreset> MatchLightPreset(const kb::scene::LightComponent& light) noexcept {
    for (int index = 0; index < kLightPresetCount; ++index) {
        // A preset applied to a copy changes only the values it sets: the copy equals the light exactly when the
        // light already has them.
        kb::scene::LightComponent probe = light;
        ApplyLightPreset(probe, static_cast<LightPreset>(index));
        const auto close = [](float a, float b) { return std::abs(a - b) <= 0.0005F * std::max(1.0F, std::abs(b)); };
        if (probe.kind == light.kind && probe.useColorTemperature == light.useColorTemperature &&
            close(probe.colorTemperatureKelvin, light.colorTemperatureKelvin) && close(probe.intensity, light.intensity) &&
            close(probe.range, light.range) && close(probe.innerConeDegrees, light.innerConeDegrees) &&
            close(probe.outerConeDegrees, light.outerConeDegrees) && close(probe.color.x, light.color.x) &&
            close(probe.color.y, light.color.y) && close(probe.color.z, light.color.z)) {
            return static_cast<LightPreset>(index);
        }
    }
    return std::nullopt;
}

} // namespace kb::editor
