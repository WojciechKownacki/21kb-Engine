#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIScrollView {
    static constexpr std::string_view StableId = "kb21.ui.scroll-view";
    static constexpr std::uint32_t SchemaVersion = 1U;
    float scrollX = 0.0F;
    float scrollY = 0.0F;
    float scrollSensitivity = 24.0F;
    bool horizontal = true;
    bool vertical = true;
    bool inertia = true;
};

} // namespace kb::scene
