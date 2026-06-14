#include "rendering/scene_viewport_toolbar/SceneViewportToolbarState.hpp"

#if defined(_WIN32)
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarLayout.hpp"

#include <algorithm>
#include <chrono>

namespace kb::editor {
namespace {

struct SceneViewportFpsMeter {
    std::chrono::steady_clock::time_point windowStart{};
    std::chrono::steady_clock::time_point lastFrame{};
    int frames = 0;
    int fps = 0;
    bool initialized = false;
};

[[nodiscard]] SceneViewportFpsMeter& FpsMeter() noexcept {
    static SceneViewportFpsMeter meter;
    return meter;
}

[[nodiscard]] SceneViewportToolbarRenderStats& LastRenderStats() noexcept {
    static SceneViewportToolbarRenderStats stats;
    return stats;
}

[[nodiscard]] SceneViewportToolbarInfoHover& LastInfoHover() noexcept {
    static SceneViewportToolbarInfoHover hover = SceneViewportToolbarInfoHover::None;
    return hover;
}

[[nodiscard]] bool EmptyRect(const RECT& rect) noexcept {
    return rect.right <= rect.left || rect.bottom <= rect.top;
}

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

} // namespace

void SceneViewportToolbarState::RecordPresentedFrame() noexcept {
    using Clock = std::chrono::steady_clock;
    using Seconds = std::chrono::duration<double>;

    SceneViewportFpsMeter& meter = FpsMeter();
    const Clock::time_point now = Clock::now();
    if (!meter.initialized) {
        meter.windowStart = now;
        meter.lastFrame = now;
        meter.frames = 1;
        meter.initialized = true;
        return;
    }

    ++meter.frames;
    meter.lastFrame = now;
    const double elapsed = Seconds(now - meter.windowStart).count();
    if (elapsed >= 0.5) {
        meter.fps = std::max(0, static_cast<int>((static_cast<double>(meter.frames) / elapsed) + 0.5));
        meter.windowStart = now;
        meter.frames = 0;
    }
}

int SceneViewportToolbarState::CurrentPresentedFps() noexcept {
    using Clock = std::chrono::steady_clock;
    using Seconds = std::chrono::duration<double>;

    const SceneViewportFpsMeter& meter = FpsMeter();
    if (!meter.initialized) {
        return 0;
    }

    const Clock::time_point now = Clock::now();
    if (Seconds(now - meter.lastFrame).count() > 0.75) {
        return 0;
    }
    return meter.fps;
}

void SceneViewportToolbarState::RecordRenderStats(SceneViewportToolbarRenderStats stats) noexcept {
    LastRenderStats() = stats;
}

SceneViewportToolbarRenderStats SceneViewportToolbarState::RenderStats() noexcept {
    return LastRenderStats();
}

SceneViewportToolbarInfoHover SceneViewportToolbarState::InfoHover() noexcept {
    return LastInfoHover();
}

bool SceneViewportToolbarState::UpdateInfoHover(const RECT& content, int x, int y) noexcept {
    SceneViewportToolbarInfoHover& hover = LastInfoHover();
    if (EmptyRect(content)) {
        if (hover == SceneViewportToolbarInfoHover::None) {
            return false;
        }
        hover = SceneViewportToolbarInfoHover::None;
        return true;
    }

    const SceneViewportToolbarRects rects = SceneViewportToolbarLayout::Resolve(content);
    SceneViewportToolbarInfoHover next = SceneViewportToolbarInfoHover::None;
    if (PointInRect(rects.renderStats, x, y)) {
        next = SceneViewportToolbarInfoHover::RenderStats;
    } else if (PointInRect(rects.pipelineStats, x, y)) {
        next = SceneViewportToolbarInfoHover::PipelineStats;
    }

    if (hover == next) {
        return false;
    }
    hover = next;
    return true;
}

} // namespace kb::editor

#endif
