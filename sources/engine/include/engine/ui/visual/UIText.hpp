#pragma once

#include "engine/math/EngineMath.hpp"

#include <cstdint>

namespace kb::scene {

enum class UITextHorizontalAlignment : std::uint8_t {
    Left,
    Center,
    Right,
};

enum class UITextVerticalAlignment : std::uint8_t {
    Top,
    Center,
    Bottom,
};

enum class UITextWrapMode : std::uint8_t {
    NoWrap,
    Word,
    Character,
};

struct UIText {
    std::uint64_t fontAssetId = 0U;
    float fontSize = 16.0F;
    kb::math::Color color{};
    UITextHorizontalAlignment horizontalAlignment = UITextHorizontalAlignment::Left;
    UITextVerticalAlignment verticalAlignment = UITextVerticalAlignment::Top;
    UITextWrapMode wrapMode = UITextWrapMode::Word;
};

} // namespace kb::scene
