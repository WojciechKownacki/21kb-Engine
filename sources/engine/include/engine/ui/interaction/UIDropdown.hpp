#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIDropdown {
    static constexpr std::string_view StableId = "kb21.ui.dropdown";
    static constexpr std::uint32_t SchemaVersion = 2U;
    std::uint32_t selectedIndex = 0U;
    // How many option rows the open list shows at once. 0 shows every option. A positive value
    // bounds the popup and clips the rest, so a settings list of twenty resolutions cannot
    // cover the screen it belongs to; navigation scrolls the clipped rows into view.
    std::uint32_t maxVisibleOptions = 0U;
};

} // namespace kb::scene
