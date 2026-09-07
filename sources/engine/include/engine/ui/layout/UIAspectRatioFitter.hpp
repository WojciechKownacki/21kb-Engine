#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

enum class UIAspectFitMode : std::uint8_t {
    None,
    WidthControlsHeight,
    HeightControlsWidth,
    FitInsideParent,
    EnvelopeParent,
};

struct UIAspectRatioFitter {
    static constexpr std::string_view StableId = "kb21.ui.aspect-ratio-fitter";
    static constexpr std::uint32_t SchemaVersion = 1U;

    UIAspectFitMode mode = UIAspectFitMode::None;
    float aspectRatio = 1.0F;
};

} // namespace kb::scene
