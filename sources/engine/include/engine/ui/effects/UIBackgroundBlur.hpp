#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIBackgroundBlur {
    static constexpr std::string_view StableId = "kb21.ui.background-blur";
    static constexpr std::uint32_t SchemaVersion = 1U;

    float radius = 0.0F;
};

} // namespace kb::scene
