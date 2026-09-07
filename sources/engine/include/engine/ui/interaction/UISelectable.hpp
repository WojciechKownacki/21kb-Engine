#pragma once

#include "engine/math/EngineMath.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace kb::scene {

enum class UINavigationMode : std::uint8_t {
    None,
    Automatic,
    Explicit,
};

struct UISelectable {
    static constexpr std::string_view StableId = "kb21.ui.selectable";
    static constexpr std::uint32_t SchemaVersion = 1U;
    static constexpr std::size_t MaxEventNameBytes = 64U;

    bool raycastTarget = true;
    bool interactable = true;
    UINavigationMode navigationMode = UINavigationMode::Automatic;
    std::uint64_t navigationUp = 0U;
    std::uint64_t navigationDown = 0U;
    std::uint64_t navigationLeft = 0U;
    std::uint64_t navigationRight = 0U;
    std::array<char, MaxEventNameBytes> eventName{};
    kb::math::Color normalColor{};
    kb::math::Color highlightedColor{0.88F, 0.88F, 0.88F, 1.0F};
    kb::math::Color pressedColor{0.72F, 0.72F, 0.72F, 1.0F};
    kb::math::Color selectedColor{0.82F, 0.86F, 1.0F, 1.0F};
    kb::math::Color disabledColor{0.52F, 0.52F, 0.52F, 0.5F};
    float colorFadeSeconds = 0.1F;
};

[[nodiscard]] inline std::string_view UIEventName(const UISelectable& value) noexcept {
    const auto end = std::find(value.eventName.begin(), value.eventName.end(), '\0');
    return std::string_view{value.eventName.data(), static_cast<std::size_t>(end - value.eventName.begin())};
}

[[nodiscard]] inline bool SetUIEventName(UISelectable& value, std::string_view name) noexcept {
    if (name.size() >= value.eventName.size() || name.find('\0') != std::string_view::npos) return false;
    value.eventName.fill('\0');
    std::copy(name.begin(), name.end(), value.eventName.begin());
    return true;
}

} // namespace kb::scene
