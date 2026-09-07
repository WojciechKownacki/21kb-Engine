#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UILayoutElement {
    static constexpr std::string_view StableId = "kb21.ui.layout-element";
    static constexpr std::uint32_t SchemaVersion = 1U;

    float minimumWidth = -1.0F;
    float minimumHeight = -1.0F;
    float preferredWidth = -1.0F;
    float preferredHeight = -1.0F;
    float flexibleWidth = -1.0F;
    float flexibleHeight = -1.0F;
    std::int32_t layoutPriority = 1;
    bool ignoreLayout = false;
};

} // namespace kb::scene
