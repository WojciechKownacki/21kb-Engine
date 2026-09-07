#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/ui/layout/UIEdges.hpp"
#include "engine/ui/layout/UILayoutAlignment.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIGridLayout {
    static constexpr std::string_view StableId = "kb21.ui.grid-layout";
    static constexpr std::uint32_t SchemaVersion = 1U;

    UIEdges padding{};
    kb::math::Vec2 spacing{};
    kb::math::Vec2 cellSize{100.0F, 100.0F};
    std::uint32_t columns = 1U;
    UIAlignment horizontalAlignment = UIAlignment::Start;
    UIAlignment verticalAlignment = UIAlignment::Start;
};

} // namespace kb::scene
