#pragma once

#include <cmath>
#include <cstdint>

namespace kb::editor {

struct MaterialGraphInteractionPolicy final {
    static constexpr int DragThresholdPixels = 4;
    static constexpr std::int32_t GridSpacing = 32;
    static constexpr float MinimumZoom = 0.10F;
    static constexpr float MaximumZoom = 2.50F;
    static constexpr float DefaultZoom = 0.72F;

    [[nodiscard]] static constexpr bool CrossedDragThreshold(int deltaX, int deltaY) noexcept {
        return deltaX <= -DragThresholdPixels || deltaX >= DragThresholdPixels ||
            deltaY <= -DragThresholdPixels || deltaY >= DragThresholdPixels;
    }

    [[nodiscard]] static std::int32_t SnapCoordinate(std::int32_t value) noexcept {
        return static_cast<std::int32_t>(
            std::lround(static_cast<double>(value) / static_cast<double>(GridSpacing)) * GridSpacing);
    }
};

} // namespace kb::editor
