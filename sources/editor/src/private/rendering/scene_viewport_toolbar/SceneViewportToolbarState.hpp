#pragma once

#include "rendering/SceneViewportToolbarRenderer.hpp"

#include <cstdint>

namespace kb::editor {

#if defined(_WIN32)

class SceneViewportToolbarState {
public:
    SceneViewportToolbarState() = delete;

    // The cost of producing one viewport frame. The displayed rate is its reciprocal,
    // so it reports how fast the viewport draws rather than how often the editor chose to.
    static void RecordFrameMilliseconds(double milliseconds) noexcept;
    [[nodiscard]] static int CurrentPresentedFps() noexcept;
};

#endif

} // namespace kb::editor
