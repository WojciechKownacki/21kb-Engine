#pragma once

#include "engine/ui/interaction/UIRange.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UISlider {
    static constexpr std::string_view StableId = "kb21.ui.slider";
    static constexpr std::uint32_t SchemaVersion = 1U;
    float minimum = 0.0F;
    float maximum = 1.0F;
    float value = 0.0F;
    UIAxisDirection direction = UIAxisDirection::LeftToRight;
    bool wholeNumbers = false;
};

} // namespace kb::scene
