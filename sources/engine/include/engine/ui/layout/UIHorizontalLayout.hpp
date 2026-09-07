#pragma once

#include "engine/ui/layout/UIEdges.hpp"
#include "engine/ui/layout/UILayoutAlignment.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIHorizontalLayout {
    static constexpr std::string_view StableId = "kb21.ui.horizontal-layout";
    static constexpr std::uint32_t SchemaVersion = 1U;

    UIEdges padding{};
    float spacing = 0.0F;
    UIAlignment horizontalAlignment = UIAlignment::Start;
    UIAlignment verticalAlignment = UIAlignment::Start;
    bool controlChildWidth = false;
    bool controlChildHeight = false;
    bool expandChildWidth = false;
    bool expandChildHeight = false;
};

} // namespace kb::scene
