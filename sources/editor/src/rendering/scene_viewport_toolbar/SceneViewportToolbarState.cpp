#include "rendering/scene_viewport_toolbar/SceneViewportToolbarState.hpp"

#if defined(_WIN32)
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarLayout.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace kb::editor {
namespace {

struct SceneViewportFrameMeter {
    double smoothedMilliseconds = 0.0;
    int fps = 0;
};

// Frame cost is the measurement; the rate is derived from it. Counting how many
// frames appeared in the last second instead would measure how often the editor
// chose to redraw, which on an on-demand editor is a property of the user's mouse.
constexpr double kSmoothingWeight = 0.1;

[[nodiscard]] SceneViewportFrameMeter& FrameMeter() noexcept {
    static SceneViewportFrameMeter meter;
    return meter;
}

} // namespace

void SceneViewportToolbarState::RecordFrameMilliseconds(double milliseconds) noexcept {
    if (!(milliseconds > 0.0)) {
        return;
    }

    SceneViewportFrameMeter& meter = FrameMeter();
    meter.smoothedMilliseconds = meter.smoothedMilliseconds <= 0.0
        ? milliseconds
        : (meter.smoothedMilliseconds * (1.0 - kSmoothingWeight)) + (milliseconds * kSmoothingWeight);
    meter.fps = std::max(0, static_cast<int>((1000.0 / meter.smoothedMilliseconds) + 0.5));
}

int SceneViewportToolbarState::CurrentPresentedFps() noexcept {
    // Held while the viewport sits idle: standing still is not a performance collapse,
    // and the last frame's cost remains the honest answer to how fast it draws.
    return FrameMeter().fps;
}

} // namespace kb::editor

#endif
