#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/ui/layout/UIEdges.hpp"

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

enum class UISelectableTransition : std::uint8_t {
    None,
    // Tints the target graphic with the state colours.
    ColorTint,
    // Swaps the target graphic's image for the state image.
    SpriteSwap,
};

struct UISelectable {
    static constexpr std::string_view StableId = "kb21.ui.selectable";
    static constexpr std::uint32_t SchemaVersion = 3U;
    static constexpr std::size_t MaxEventNameBytes = 64U;
    static constexpr std::size_t MaxTooltipUtf8Bytes = 256U;

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
    // The widget that shows this selectable's state colours. With none, the selectable's own graphics are
    // tinted.
    std::uint64_t targetGraphic = 0U;
    UISelectableTransition transition = UISelectableTransition::ColorTint;
    // Sprite Swap images; a state without one keeps the normal image.
    std::uint64_t highlightedImage = 0U;
    std::uint64_t pressedImage = 0U;
    std::uint64_t selectedImage = 0U;
    std::uint64_t disabledImage = 0U;
    // Grows (positive) or shrinks (negative) the clickable area past the widget's edges, in canvas units.
    UIEdges raycastPadding{};
    // Shown in a bubble next to the pointer after it rests on the widget.
    std::array<char, MaxTooltipUtf8Bytes> tooltip{};
    // Lets the widget be dragged: it reports OnUIDragBegan, OnUIDragged and OnUIDragEnded, and the widget
    // under the pointer on release reports OnUIDropped with the dragged widget as `other`.
    bool draggable = false;
    // The object that receives the action event named by `eventName` on click. None sends it to this widget.
    std::uint64_t eventTarget = 0U;
};

[[nodiscard]] inline std::string_view UITooltipText(const UISelectable& value) noexcept {
    const auto end = std::find(value.tooltip.begin(), value.tooltip.end(), '\0');
    return std::string_view{value.tooltip.data(), static_cast<std::size_t>(end - value.tooltip.begin())};
}
[[nodiscard]] inline bool SetUITooltipText(UISelectable& value, std::string_view text) noexcept {
    if (text.size() >= value.tooltip.size() || text.find('\0') != std::string_view::npos) return false;
    value.tooltip.fill('\0');
    std::copy(text.begin(), text.end(), value.tooltip.begin());
    return true;
}
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
