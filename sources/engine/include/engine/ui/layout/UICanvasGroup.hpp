#pragma once

#include "engine/math/EngineMath.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UICanvasGroup {
    static constexpr std::string_view StableId = "kb21.ui.canvas-group";
    static constexpr std::uint32_t SchemaVersion = 2U;

    float opacity = 1.0F;
    bool interactable = true;
    bool blocksRaycasts = true;
    bool ignoreParentGroups = false;
    // Show/Hide animation. Turning `visible` off fades the group out while it moves by `hiddenOffset` and
    // scales to `hiddenScale`; turning it on plays that back. A hidden group takes no input.
    bool visible = true;
    float transitionSeconds = 0.0F;
    kb::math::Vec2 hiddenOffset{};
    float hiddenScale = 1.0F;
};

} // namespace kb::scene
