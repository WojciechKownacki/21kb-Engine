#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIToggle {
    static constexpr std::string_view StableId = "kb21.ui.toggle";
    static constexpr std::uint32_t SchemaVersion = 2U;
    bool toggled = false;
    // The widget that shows the on state - a checkmark. It is drawn only while the toggle is on. With
    // none, the toggle draws its own indicator.
    std::uint64_t graphic = 0U;
};

} // namespace kb::scene
