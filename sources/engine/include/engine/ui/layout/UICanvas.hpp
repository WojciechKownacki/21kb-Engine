#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UICanvas {
    static constexpr std::string_view StableId = "kb21.ui.canvas";
    static constexpr std::uint32_t SchemaVersion = 1U;

    std::int32_t sortingOrder = 0;
    bool pixelPerfect = false;
};

} // namespace kb::scene
