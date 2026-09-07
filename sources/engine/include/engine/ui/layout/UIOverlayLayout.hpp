#pragma once

#include "engine/ui/layout/UIEdges.hpp"
#include "engine/ui/layout/UILayoutAlignment.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIOverlayLayout {
    static constexpr std::string_view StableId = "kb21.ui.overlay-layout";
    static constexpr std::uint32_t SchemaVersion = 1U;

    UIEdges padding{};
    UIAlignment horizontalAlignment = UIAlignment::Stretch;
    UIAlignment verticalAlignment = UIAlignment::Stretch;
};

} // namespace kb::scene
