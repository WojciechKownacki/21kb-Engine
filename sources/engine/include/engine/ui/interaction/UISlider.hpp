#pragma once

#include "engine/ui/interaction/UIRange.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UISlider {
    static constexpr std::string_view StableId = "kb21.ui.slider";
    static constexpr std::uint32_t SchemaVersion = 2U;
    float minimum = 0.0F;
    float maximum = 1.0F;
    float value = 0.0F;
    UIAxisDirection direction = UIAxisDirection::LeftToRight;
    bool wholeNumbers = false;
    // Widgets the slider drives: the fill stretches from the start to the value and the handle sits at
    // the value. With neither, the slider draws its own track and thumb.
    std::uint64_t fillRect = 0U;
    std::uint64_t handleRect = 0U;
};

} // namespace kb::scene
