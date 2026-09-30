#pragma once

#include "engine/ui/interaction/UIRange.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIProgressBar {
    static constexpr std::string_view StableId = "kb21.ui.progress-bar";
    static constexpr std::uint32_t SchemaVersion = 2U;
    float minimum = 0.0F;
    float maximum = 1.0F;
    float value = 0.0F;
    UIAxisDirection direction = UIAxisDirection::LeftToRight;
    // The widget stretched from the start to the value. With none, the first child is.
    std::uint64_t fillRect = 0U;
};

} // namespace kb::scene
