#pragma once

// Colours a person types or picks (hex codes, colour pickers) are sRGB, as in Unity and Unreal.
// Material and light colours are stored and shaded in linear space. These are the only
// conversions between the two (IEC 61966-2-1 transfer function).

#include <array>
#include <cmath>
#include <optional>
#include <string_view>

namespace kb::math {

[[nodiscard]] inline float SrgbToLinear(const float srgb) noexcept {
    return srgb <= 0.04045F ? srgb / 12.92F : std::pow((srgb + 0.055F) / 1.055F, 2.4F);
}

[[nodiscard]] inline float LinearToSrgb(const float linear) noexcept {
    return linear <= 0.0031308F ? linear * 12.92F : 1.055F * std::pow(linear, 1.0F / 2.4F) - 0.055F;
}

// "#RRGGBB" or "#RRGGBBAA" (the leading '#' is optional) -> sRGB 0..1 with alpha (1 when absent).
[[nodiscard]] inline std::optional<std::array<float, 4>> ParseHexColor(std::string_view text) noexcept {
    if (!text.empty() && text.front() == '#') text.remove_prefix(1);
    if (text.size() != 6U && text.size() != 8U) return std::nullopt;
    const auto nibble = [](const char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::array<float, 4> color{ 0.0F, 0.0F, 0.0F, 1.0F };
    for (std::size_t channel = 0U; channel * 2U < text.size(); ++channel) {
        const int high = nibble(text[channel * 2U]);
        const int low = nibble(text[channel * 2U + 1U]);
        if (high < 0 || low < 0) return std::nullopt;
        color[channel] = static_cast<float>(high * 16 + low) / 255.0F;
    }
    return color;
}

} // namespace kb::math
