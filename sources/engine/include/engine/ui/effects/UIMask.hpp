#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIMask {
    static constexpr std::string_view StableId = "kb21.ui.mask";
    static constexpr std::uint32_t SchemaVersion = 2U;

    bool showGraphic = true;
    // Width, in canvas units, over which children fade out towards the mask's edge.
    float softness = 0.0F;
};

} // namespace kb::scene
