#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIProgressBar {
    static constexpr std::string_view StableId = "kb21.ui.progress-bar";
    static constexpr std::uint32_t SchemaVersion = 1U;
    float minimum = 0.0F;
    float maximum = 1.0F;
    float value = 0.0F;
};

} // namespace kb::scene
