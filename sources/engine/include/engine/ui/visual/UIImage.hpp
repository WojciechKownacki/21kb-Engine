#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/ui/layout/UIEdges.hpp"

#include <cstdint>

namespace kb::scene {

enum class UIImageScaleMode : std::uint8_t {
    Stretch,
    Contain,
    Cover,
};

struct UIImage {
    std::uint64_t imageAssetId = 0U;
    kb::math::Rect uvRect{0.0F, 0.0F, 1.0F, 1.0F};
    UIImageScaleMode scaleMode = UIImageScaleMode::Stretch;
    bool preserveAspect = false;
    UIEdges nineSlice{};
};

} // namespace kb::scene
