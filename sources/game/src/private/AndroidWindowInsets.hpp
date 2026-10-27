#pragma once

#include "engine/ui/layout/UIEdges.hpp"
#include <game-activity/GameActivity.h>
#include <algorithm>

namespace kb::game {

// The Android window policy keeps the render surface edge-to-edge. Insets are
// therefore surface pixels, including an IME that overlays rather than resizes it.
[[nodiscard]] inline scene::UIEdges ReadAndroidWindowInsets(GameActivity& activity) noexcept {
    scene::UIEdges result{};
    for (const auto type : {GAMECOMMON_INSETS_TYPE_SYSTEM_BARS, GAMECOMMON_INSETS_TYPE_DISPLAY_CUTOUT,
             GAMECOMMON_INSETS_TYPE_MANDATORY_SYSTEM_GESTURES, GAMECOMMON_INSETS_TYPE_IME,
             GAMECOMMON_INSETS_TYPE_WATERFALL}) {
        ARect insets{};
        GameActivity_getWindowInsets(&activity, type, &insets);
        result.left = std::max(result.left, static_cast<float>(insets.left));
        result.top = std::max(result.top, static_cast<float>(insets.top));
        result.right = std::max(result.right, static_cast<float>(insets.right));
        result.bottom = std::max(result.bottom, static_cast<float>(insets.bottom));
    }
    return result;
}

} // namespace kb::game
