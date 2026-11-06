#pragma once

#include "rendering/scene_viewport_toolbar/TerrainToolbarDrawer.hpp"

#include <array>
#include <cstdint>

// The geometry the terrain drawer is laid out with, shared by its layout and its painting.
namespace kb::editor::terrain_toolbar {

constexpr int kPad = 10;
constexpr int kRowHeight = 26;
constexpr int kRowGap = 6;
constexpr int kChipGap = 4;
constexpr int kLabelWidth = 92;
constexpr std::array<std::uint32_t, 5U> kResolutions{ 65U, 129U, 257U, 513U, 1025U };

[[nodiscard]] inline RECT Box(int left, int top, int width) noexcept {
    return RECT{ left, top, left + width, top + kRowHeight };
}

[[nodiscard]] inline TerrainStepperRects Stepper(int& cursor, int top, int valueWidth) noexcept {
    TerrainStepperRects stepper;
    stepper.minus = Box(cursor, top, 24);
    stepper.value = Box(stepper.minus.right + kChipGap, top, valueWidth);
    stepper.plus = Box(stepper.value.right + kChipGap, top, 24);
    cursor = stepper.plus.right + 10;
    return stepper;
}

// The running job's bar and its Cancel button, from `left` on row `top`.
inline void LayoutJob(TerrainViewportToolbarRects& rects, int left, int top) noexcept {
    rects.jobBar = Box(left, top, 230);
    rects.cancelButton = Box(rects.jobBar.right + kChipGap, top, 78);
}

} // namespace kb::editor::terrain_toolbar
