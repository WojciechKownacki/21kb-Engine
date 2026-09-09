#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIWidgetSwitcher {
    static constexpr std::string_view StableId = "kb21.ui.widget-switcher";
    static constexpr std::uint32_t SchemaVersion = 1U;
    std::uint32_t visibleChildIndex = 0U;
};

} // namespace kb::scene
