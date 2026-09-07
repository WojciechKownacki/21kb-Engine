#pragma once

#include "engine/math/EngineMath.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UISprite {
    static constexpr std::string_view StableId = "kb21.ui.sprite";
    static constexpr std::uint32_t SchemaVersion = 1U;

    std::uint64_t spriteAssetId = 0U;
    kb::math::Color color{};
    bool preserveAspect = true;
};

} // namespace kb::scene
