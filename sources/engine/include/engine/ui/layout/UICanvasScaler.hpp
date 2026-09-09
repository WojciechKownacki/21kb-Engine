#pragma once

#include "engine/math/EngineMath.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

enum class UICanvasScaleMode : std::uint8_t {
    ConstantPixelSize,
    ScaleWithScreenSize,
};

struct UICanvasScaler {
    static constexpr std::string_view StableId = "kb21.ui.canvas-scaler";
    static constexpr std::uint32_t SchemaVersion = 1U;

    UICanvasScaleMode scaleMode = UICanvasScaleMode::ScaleWithScreenSize;
    kb::math::Vec2 referenceResolution{1920.0F, 1080.0F};
    float scaleFactor = 1.0F;
    float matchWidthOrHeight = 0.5F;
};

} // namespace kb::scene
