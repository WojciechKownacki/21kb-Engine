#pragma once

#include "engine/ui/interaction/UIRange.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIScrollbar {
    static constexpr std::string_view StableId = "kb21.ui.scrollbar";
    static constexpr std::uint32_t SchemaVersion = 1U;
    float value = 0.0F;
    float size = 0.2F;
    UIAxisDirection direction = UIAxisDirection::LeftToRight;
};

} // namespace kb::scene
