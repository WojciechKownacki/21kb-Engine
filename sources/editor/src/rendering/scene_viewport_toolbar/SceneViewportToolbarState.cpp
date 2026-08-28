#include "rendering/scene_viewport_toolbar/SceneViewportToolbarState.hpp"

#if defined(_WIN32)

#include <algorithm>
#include <chrono>

namespace kb::editor {
namespace {

struct SceneViewportFrameMeter {
    double smoothedMilliseconds = 0.0;
    int fps = 0;
    bool hasSample = false;
    std::chrono::steady_clock::time_point lastFrame{};
    // Set once the counter has been repainted to say it is no longer live, so the
    // crossing is reported once per quiet spell rather than on every poll.
    bool idleReported = false;
};

// Frame cost is the measurement; the rate is derived from it. Counting how many
// frames appeared in the last second instead would measure how often the editor
// chose to redraw, which on an on-demand editor is a property of the user's mouse.
constexpr double kSmoothingWeight = 0.1;

[[nodiscard]] SceneViewportFrameMeter& FrameMeter() noexcept {
    static SceneViewportFrameMeter meter;
    return meter;
}

[[nodiscard]] bool IsLive(
    const SceneViewportFrameMeter& meter,
    std::chrono::steady_clock::time_point now) noexcept {
    return meter.hasSample && (now - meter.lastFrame) < SceneViewportToolbarState::kLiveFor;
}

} // namespace

void SceneViewportToolbarState::RecordFrameMilliseconds(double milliseconds) noexcept {
    RecordFrameMilliseconds(milliseconds, std::chrono::steady_clock::now());
}

void SceneViewportToolbarState::RecordFrameMilliseconds(
    double milliseconds, std::chrono::steady_clock::time_point at) noexcept {
    if (!(milliseconds > 0.0)) {
        return;
    }

    SceneViewportFrameMeter& meter = FrameMeter();
    meter.smoothedMilliseconds = meter.smoothedMilliseconds <= 0.0
        ? milliseconds
        : (meter.smoothedMilliseconds * (1.0 - kSmoothingWeight)) + (milliseconds * kSmoothingWeight);
    meter.fps = std::max(0, static_cast<int>((1000.0 / meter.smoothedMilliseconds) + 0.5));
    meter.hasSample = true;
    meter.lastFrame = at;
    meter.idleReported = false;
}

SceneViewportToolbarState::FrameRateReading SceneViewportToolbarState::CurrentReading() noexcept {
    return CurrentReading(std::chrono::steady_clock::now());
}

SceneViewportToolbarState::FrameRateReading SceneViewportToolbarState::CurrentReading(
    std::chrono::steady_clock::time_point now) noexcept {
    const SceneViewportFrameMeter& meter = FrameMeter();
    // The number is kept while the viewport sits idle: standing still is not a
    // performance collapse, and the last frame's cost remains the honest answer to how
    // fast it draws. What must not be kept is the claim that it is a current reading.
    return FrameRateReading{ .fps = meter.fps, .live = IsLive(meter, now) };
}

bool SceneViewportToolbarState::ConsumeIdleTransition() noexcept {
    return ConsumeIdleTransition(std::chrono::steady_clock::now());
}

bool SceneViewportToolbarState::ConsumeIdleTransition(
    std::chrono::steady_clock::time_point now) noexcept {
    SceneViewportFrameMeter& meter = FrameMeter();
    if (!meter.hasSample || IsLive(meter, now) || meter.idleReported) {
        return false;
    }
    meter.idleReported = true;
    return true;
}

void SceneViewportToolbarState::Reset() noexcept {
    FrameMeter() = SceneViewportFrameMeter{};
}

} // namespace kb::editor

#endif
