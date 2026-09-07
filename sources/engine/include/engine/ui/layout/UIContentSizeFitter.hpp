#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

enum class UIFitMode : std::uint8_t {
    Unconstrained,
    MinimumSize,
    PreferredSize,
};

struct UIContentSizeFitter {
    static constexpr std::string_view StableId = "kb21.ui.content-size-fitter";
    static constexpr std::uint32_t SchemaVersion = 1U;

    UIFitMode horizontalFit = UIFitMode::Unconstrained;
    UIFitMode verticalFit = UIFitMode::Unconstrained;
};

} // namespace kb::scene
