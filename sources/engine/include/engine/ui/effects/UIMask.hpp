#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIMask {
    static constexpr std::string_view StableId = "kb21.ui.mask";
    static constexpr std::uint32_t SchemaVersion = 1U;

    bool showGraphic = true;
};

} // namespace kb::scene
