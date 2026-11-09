#pragma once

#include "engine/math/EngineMath.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIRectTransform {
    static constexpr std::string_view StableId = "kb21.ui.rect-transform";
    static constexpr std::uint32_t SchemaVersion = 2U;

    kb::math::Vec2 anchorMin{};
    kb::math::Vec2 anchorMax{};
    kb::math::Vec2 offsetMin{};
    kb::math::Vec2 offsetMax{100.0F, 100.0F};
    kb::math::Vec2 pivot{0.5F, 0.5F};
    kb::math::Vec2 scale{1.0F, 1.0F};
    float rotationDegrees = 0.0F;
    std::int32_t zOrder = 0;
    // Theme overrides are semicolon-separated stable component ID and property pairs,
    // e.g. "kb21.ui.text:color.r;kb21.ui.text:fontSize".
    std::uint64_t themeAssetId = 0U;
    std::array<char, 129U> themeVariant{};
    std::array<char, 1024U> themeOverrides{};
};

[[nodiscard]] inline std::string_view UIRectThemeVariant(const UIRectTransform& value) noexcept {
    const auto end = std::find(value.themeVariant.begin(), value.themeVariant.end(), '\0');
    return {value.themeVariant.data(), static_cast<std::size_t>(end - value.themeVariant.begin())};
}
[[nodiscard]] inline std::string_view UIRectThemeOverrides(const UIRectTransform& value) noexcept {
    const auto end = std::find(value.themeOverrides.begin(), value.themeOverrides.end(), '\0');
    return {value.themeOverrides.data(), static_cast<std::size_t>(end - value.themeOverrides.begin())};
}
[[nodiscard]] inline bool SetUIRectThemeVariant(UIRectTransform& value, std::string_view text) noexcept {
    if (text.size() >= value.themeVariant.size() || text.find('\0') != std::string_view::npos) return false;
    value.themeVariant.fill('\0'); std::copy(text.begin(), text.end(), value.themeVariant.begin()); return true;
}
[[nodiscard]] inline bool SetUIRectThemeOverrides(UIRectTransform& value, std::string_view text) noexcept {
    if (text.size() >= value.themeOverrides.size() || text.find('\0') != std::string_view::npos) return false;
    value.themeOverrides.fill('\0'); std::copy(text.begin(), text.end(), value.themeOverrides.begin()); return true;
}

} // namespace kb::scene
