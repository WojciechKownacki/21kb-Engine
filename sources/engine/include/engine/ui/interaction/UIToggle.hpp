#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIToggle {
    static constexpr std::string_view StableId = "kb21.ui.toggle";
    static constexpr std::uint32_t SchemaVersion = 3U;
    bool toggled = false;
    // The widget that shows the on state - a checkmark. It is drawn only while the toggle is on. With
    // none, the toggle draws its own indicator.
    std::uint64_t graphic = 0U;
    // Toggles naming the same group object work as radio buttons: switching one on switches the others
    // off. Unless the group allows switching off, the one that is on stays on when clicked again.
    std::uint64_t group = 0U;
    bool allowSwitchOff = false;
};

} // namespace kb::scene
