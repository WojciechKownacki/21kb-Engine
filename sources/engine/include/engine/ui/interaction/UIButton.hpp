#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIButton {
    static constexpr std::string_view StableId = "kb21.ui.button";
    static constexpr std::uint32_t SchemaVersion = 1U;
    bool submitOnRelease = true;
};

} // namespace kb::scene
