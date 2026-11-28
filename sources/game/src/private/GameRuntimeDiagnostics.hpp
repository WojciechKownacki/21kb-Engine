#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bgfx {
struct Stats;
}

namespace kb::scene {
class Scene;
}
namespace kb::script {
class ScriptModule;
}

namespace kb::game {

[[nodiscard]] std::vector<std::string> DrainGameRuntimeErrors(
    kb::scene::Scene& scene, kb::script::ScriptModule* script);

struct GpuViewTime {
    std::string name;
    std::uint16_t view = 0U;
    double milliseconds = 0.0;
};

// The GPU time of every view the renderer timed in the frame the statistics describe, in view order. Empty without a
// GPU timer or when per-view profiling is off.
[[nodiscard]] std::vector<GpuViewTime> ReadGpuViewTimes(const bgfx::Stats& stats);

// One log line with the frame's GPU and CPU times, the waits between the threads and the views, slowest first.
[[nodiscard]] std::string DescribeGpuFrame(const bgfx::Stats& stats);

// Frame intervals over a report window: the frame count, the mean and the slowest interval.
class FrameTimeWindow {
public:
    explicit FrameTimeWindow(std::chrono::steady_clock::duration length) noexcept : length_(length) {}
    // Records the end of one frame. Once the window has lasted its length it returns the report and starts the next one.
    [[nodiscard]] std::optional<std::string> Add(std::chrono::steady_clock::time_point frameEnd);
    // Forgets the running window, so a pause is not counted as a frame.
    void Reset() noexcept;

private:
    std::chrono::steady_clock::duration length_;
    std::optional<std::chrono::steady_clock::time_point> start_;
    std::chrono::steady_clock::time_point last_{};
    std::uint32_t frames_ = 0U;
    double slowestMilliseconds_ = 0.0;
};

} // namespace kb::game
