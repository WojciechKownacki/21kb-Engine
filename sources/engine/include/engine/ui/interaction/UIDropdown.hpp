#pragma once

#include "engine/math/EngineMath.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kb::scene {

// One choice of a dropdown: the label the list shows and an optional icon drawn in front of it.
struct UIDropdownOption {
    static constexpr std::size_t MaxUtf8Bytes = 64U;

    std::array<char, MaxUtf8Bytes> text{};
    std::uint64_t iconAssetId = 0U;
};

// A dropdown owns its options as data. The closed control draws the selected label; the open list
// draws one row per option in the dropdown's own row style, so an author edits a list of labels
// instead of building and styling a widget per choice.
struct UIDropdown {
    static constexpr std::string_view StableId = "kb21.ui.dropdown";
    static constexpr std::uint32_t SchemaVersion = 3U;
    static constexpr std::size_t MaxOptions = 32U;

    std::uint32_t selectedIndex = 0U;
    // How many option rows the open list shows at once. 0 shows every option. A positive value
    // bounds the popup and clips the rest, so a settings list of twenty resolutions cannot
    // cover the screen it belongs to; navigation scrolls the clipped rows into view.
    std::uint32_t maxVisibleOptions = 0U;
    std::uint32_t optionCount = 0U;
    std::array<UIDropdownOption, MaxOptions> options{};
    std::uint64_t fontAssetId = 0U;
    float fontSize = 16.0F;
    kb::math::Color textColor{0.92F, 0.93F, 0.96F, 1.0F};
    kb::math::Color itemColor{0.16F, 0.18F, 0.22F, 1.0F};
    kb::math::Color itemHighlightedColor{0.24F, 0.28F, 0.36F, 1.0F};
    kb::math::Color itemSelectedColor{0.30F, 0.40F, 0.66F, 1.0F};
};

[[nodiscard]] inline std::string_view UIDropdownOptionText(const UIDropdownOption& option) noexcept {
    const auto end = std::find(option.text.begin(), option.text.end(), '\0');
    return std::string_view{option.text.data(), static_cast<std::size_t>(end - option.text.begin())};
}

[[nodiscard]] inline bool SetUIDropdownOptionText(UIDropdownOption& option, std::string_view text) noexcept {
    if (text.size() >= option.text.size() || text.find('\0') != std::string_view::npos) {
        return false;
    }
    option.text.fill('\0');
    std::copy(text.begin(), text.end(), option.text.begin());
    return true;
}

// The label of the selected option, or empty when the dropdown has no options.
[[nodiscard]] inline std::string_view UIDropdownSelectedText(const UIDropdown& dropdown) noexcept {
    return dropdown.selectedIndex < dropdown.optionCount && dropdown.optionCount <= UIDropdown::MaxOptions
               ? UIDropdownOptionText(dropdown.options[dropdown.selectedIndex])
               : std::string_view{};
}

} // namespace kb::scene
