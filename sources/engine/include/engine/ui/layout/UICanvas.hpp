#pragma once

#include "engine/math/EngineMath.hpp"

#include <cstdint>

namespace kb::scene {

enum class UICanvasScaleMode : std::uint8_t {
    ConstantPixelSize,
    ScaleWithScreenSize,
};

struct UICanvas {
    UICanvasScaleMode scaleMode = UICanvasScaleMode::ScaleWithScreenSize;
    kb::math::Vec2 referenceResolution{1920.0F, 1080.0F};
    float scaleFactor = 1.0F;
    float matchWidthOrHeight = 0.5F;
};

} // namespace kb::scene
