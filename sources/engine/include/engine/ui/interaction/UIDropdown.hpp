#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kb::scene {

// One choice of a dropdown: its label and an optional image.
struct UIDropdownOption {
    static constexpr std::size_t MaxUtf8Bytes = 64U;

    std::array<char, MaxUtf8Bytes> text{};
    std::uint64_t imageAssetId = 0U;
};

// A dropdown shows its current choice through widgets it points at and opens a list built from a
// template it points at. Nothing about the look lives here: the caption, the arrow, the list frame,
// the scroll area and the item are ordinary widgets under the dropdown.
//
//  - `templateEntity` is a hidden subtree cloned each time the list opens. It holds an item - the
//    Toggle that is the nearest ancestor of `itemText` - which is cloned once per option.
//  - `captionText` / `captionImage` show the selected option's label and image.
//  - `itemText` / `itemImage` are the parts of the item that receive each option's label and image.
struct UIDropdown {
    static constexpr std::string_view StableId = "kb21.ui.dropdown";
    static constexpr std::uint32_t SchemaVersion = 4U;
    static constexpr std::size_t MaxOptions = 32U;

    std::uint64_t templateEntity = 0U;
    std::uint64_t captionText = 0U;
    std::uint64_t captionImage = 0U;
    std::uint64_t itemText = 0U;
    std::uint64_t itemImage = 0U;
    std::uint32_t value = 0U;
    // Seconds the open list takes to fade in or out. 0 shows and hides it at once.
    float alphaFadeSpeed = 0.15F;
    std::uint32_t optionCount = 0U;
    std::array<UIDropdownOption, MaxOptions> options{};
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

// Moves option `from` to position `to`, shifting the options between. The value keeps pointing at
// the same choice, not at the same index.
[[nodiscard]] inline bool MoveUIDropdownOption(UIDropdown& dropdown, std::uint32_t from, std::uint32_t to) noexcept {
    if (from >= dropdown.optionCount || to >= dropdown.optionCount) {
        return false;
    }
    const auto begin = dropdown.options.begin();
    if (from < to) {
        std::rotate(begin + from, begin + from + 1U, begin + to + 1U);
    } else {
        std::rotate(begin + to, begin + from, begin + from + 1U);
    }
    std::uint32_t& value = dropdown.value;
    if (value == from) {
        value = to;
    } else if (from < to && value > from && value <= to) {
        --value;
    } else if (to < from && value >= to && value < from) {
        ++value;
    }
    return true;
}

// Removes one option. The value stays on the same choice; removing the chosen option chooses the one
// that took its place, or the new last option.
[[nodiscard]] inline bool RemoveUIDropdownOption(UIDropdown& dropdown, std::uint32_t index) noexcept {
    if (index >= dropdown.optionCount) {
        return false;
    }
    const auto begin = dropdown.options.begin();
    std::rotate(begin + index, begin + index + 1U, begin + dropdown.optionCount);
    --dropdown.optionCount;
    dropdown.options[dropdown.optionCount] = {};
    if (dropdown.value > index) {
        --dropdown.value;
    }
    dropdown.value = dropdown.optionCount == 0U ? 0U : std::min(dropdown.value, dropdown.optionCount - 1U);
    return true;
}

} // namespace kb::scene
