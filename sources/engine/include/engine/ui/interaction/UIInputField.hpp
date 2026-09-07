#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIInputField {
    static constexpr std::string_view StableId = "kb21.ui.input-field";
    static constexpr std::uint32_t SchemaVersion = 1U;
    std::uint32_t characterLimit = 0U;
    bool multiline = false;
    bool readOnly = false;
};

} // namespace kb::scene
