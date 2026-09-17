#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

enum class UIScrollMovement : std::uint8_t {
    // Stops at the ends of the content.
    Clamped,
    // Lets the content be pulled past an end and springs it back when released.
    Elastic,
    // Never stops.
    Unrestricted,
};

struct UIScrollView {
    static constexpr std::string_view StableId = "kb21.ui.scroll-view";
    static constexpr std::uint32_t SchemaVersion = 3U;
    float scrollX = 0.0F;
    float scrollY = 0.0F;
    float scrollSensitivity = 24.0F;
    bool horizontal = true;
    bool vertical = true;
    bool inertia = true;
    // Scrollbars kept in step with the scroll: they show the visible share and position, and dragging
    // one scrolls. Each is hidden while everything fits along its axis.
    std::uint64_t verticalScrollbar = 0U;
    std::uint64_t horizontalScrollbar = 0U;
    UIScrollMovement movementType = UIScrollMovement::Clamped;
    // Elastic only: seconds the content takes to spring back.
    float elasticity = 0.1F;
    // After a drag or a flick settles, scrolls so the nearest child lines up with the start of the view.
    bool snapToChildren = false;
};

} // namespace kb::scene
