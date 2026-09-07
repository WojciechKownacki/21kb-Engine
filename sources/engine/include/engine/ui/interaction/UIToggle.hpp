#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIToggle {
    static constexpr std::string_view StableId = "kb21.ui.toggle";
    static constexpr std::uint32_t SchemaVersion = 1U;
    bool toggled = false;
};

} // namespace kb::scene
