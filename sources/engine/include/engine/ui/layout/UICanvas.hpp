#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

enum class UICanvasRenderMode : std::uint8_t {
    // Drawn over the whole screen.
    ScreenSpace,
    // Placed in the 3D world at the canvas object's transform and seen through the active camera.
    WorldSpace,
};

struct UICanvas {
    static constexpr std::string_view StableId = "kb21.ui.canvas";
    static constexpr std::uint32_t SchemaVersion = 2U;

    std::int32_t sortingOrder = 0;
    bool pixelPerfect = false;
    UICanvasRenderMode renderMode = UICanvasRenderMode::ScreenSpace;
    // World Space only: how many canvas units make one world unit.
    float pixelsPerUnit = 100.0F;
    // Screen Space only: lays the canvas out inside the safe area the platform reports - clear of
    // notches, rounded corners and system bars - instead of the whole screen.
    bool respectSafeArea = true;
    // Which local player drives this canvas with their controller: -1 is every player plus mouse and
    // keyboard, 0-3 one player whose focus is kept apart from the others (split-screen menus).
    std::int32_t player = -1;
};

} // namespace kb::scene
