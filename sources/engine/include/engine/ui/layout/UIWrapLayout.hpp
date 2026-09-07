#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/ui/layout/UIEdges.hpp"
#include "engine/ui/layout/UILayoutAlignment.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIWrapLayout {
    static constexpr std::string_view StableId = "kb21.ui.wrap-layout";
    static constexpr std::uint32_t SchemaVersion = 1U;

    UIEdges padding{};
    kb::math::Vec2 spacing{};
    UIAlignment horizontalAlignment = UIAlignment::Start;
    UIAlignment verticalAlignment = UIAlignment::Start;
};

} // namespace kb::scene
