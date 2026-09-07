#pragma once

#include "engine/math/EngineMath.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIRawImage {
    static constexpr std::string_view StableId = "kb21.ui.raw-image";
    static constexpr std::uint32_t SchemaVersion = 1U;

    std::uint64_t imageAssetId = 0U;
    kb::math::Rect uvRect{0.0F, 0.0F, 1.0F, 1.0F};
    kb::math::Color color{};
};

} // namespace kb::scene
