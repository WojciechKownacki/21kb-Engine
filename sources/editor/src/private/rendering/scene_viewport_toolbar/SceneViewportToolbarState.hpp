#pragma once

#include <chrono>
#include <cstdint>

namespace kb::editor {

#if defined(_WIN32)

class SceneViewportToolbarState {
public:
    SceneViewportToolbarState() = delete;

    // What the counter knows. `fps` is the reciprocal of the last frame's cost, so it
    // reports how fast the viewport draws rather than how often the editor chose to.
    // `live` says whether a frame was drawn recently enough for that to still be a
    // current answer: the editor presents on demand, so a viewport nobody is touching
    // produces no frames and the reading stops being news.
    struct FrameRateReading {
        int fps = 0;
        bool live = false;
    };

    // How long the last frame's cost stays a current answer. Longer than the editor's
    // 60 Hz pacing by an order of magnitude, so ordinary interaction never blinks.
    static constexpr std::chrono::milliseconds kLiveFor{ 500 };

    // The cost of producing one viewport frame.
    static void RecordFrameMilliseconds(double milliseconds) noexcept;
    static void RecordFrameMilliseconds(
        double milliseconds, std::chrono::steady_clock::time_point at) noexcept;

    [[nodiscard]] static FrameRateReading CurrentReading() noexcept;
    [[nodiscard]] static FrameRateReading CurrentReading(
        std::chrono::steady_clock::time_point now) noexcept;

    // True exactly once each time the counter stops being live. The viewport toolbar is
    // only repainted when something presents, so without this the last live number would
    // stay on the glass forever and read as a current one.
    [[nodiscard]] static bool ConsumeIdleTransition() noexcept;
    [[nodiscard]] static bool ConsumeIdleTransition(
        std::chrono::steady_clock::time_point now) noexcept;

    // Test seam: drops every sample so one case cannot inherit another's meter.
    static void Reset() noexcept;
};

#endif

} // namespace kb::editor
