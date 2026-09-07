#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIDropdown {
    static constexpr std::string_view StableId = "kb21.ui.dropdown";
    static constexpr std::uint32_t SchemaVersion = 1U;
    std::uint32_t selectedIndex = 0U;
};

} // namespace kb::scene
