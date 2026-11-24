#pragma once

#include <array>
#include <string_view>

namespace kb::scene {

struct NetworkedVariablesComponent {
    static constexpr std::string_view StableId = "kb21.net.variables";
    std::array<float, 32U> values{};
};

} // namespace kb::scene
