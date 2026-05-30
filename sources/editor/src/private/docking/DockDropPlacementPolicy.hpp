#pragma once

#include "kb/editor/docking/DockTypes.hpp"

namespace kb::editor {

class DockDropPlacementPolicy {
public:
    DockDropPlacementPolicy() = delete;

    [[nodiscard]] static DockSplitAxis AxisForZone(DockDropZone zone) noexcept;
    [[nodiscard]] static bool IsDroppedFirst(DockDropZone zone) noexcept;
    [[nodiscard]] static float RatioForTarget(const DockDropPreview& target) noexcept;
};

} // namespace kb::editor
