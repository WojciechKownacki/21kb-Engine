#include "GameRuntimeDiagnostics.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/script/ScriptModule.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

namespace kb::game {

std::vector<std::string> DrainGameRuntimeErrors(
    kb::scene::Scene& scene, kb::script::ScriptModule* script) {
    std::vector<std::string> errors = scene.Runtime().DrainSceneSystemErrors();
    for (std::string& error : errors) {
        error = "scene runtime error: " + std::move(error);
    }
    if (script != nullptr && script->Host() != nullptr) {
        for (std::string& error : script->Host()->DrainSceneSystemDiagnostics()) {
            errors.push_back("script error: " + std::move(error));
        }
    }
    return errors;
}

std::vector<GpuViewTime> ReadGpuViewTimes(const bgfx::Stats& stats) {
    std::vector<GpuViewTime> times;
    if (stats.gpuTimerFreq <= 0 || stats.viewStats == nullptr) return times;
    times.reserve(stats.numViews);
    for (std::uint16_t index = 0U; index < stats.numViews; ++index) {
        const bgfx::ViewStats& view = stats.viewStats[index];
        if (view.gpuTimeEnd <= view.gpuTimeBegin) continue;
        times.push_back(GpuViewTime{
            .name = view.name,
            .view = view.view,
            .milliseconds = static_cast<double>(view.gpuTimeEnd - view.gpuTimeBegin) * 1000.0 /
                static_cast<double>(stats.gpuTimerFreq),
        });
    }
    return times;
}

std::string DescribeGpuFrame(const bgfx::Stats& stats) {
    std::ostringstream line;
    line << std::fixed << std::setprecision(2);
    const auto milliseconds = [](std::int64_t ticks, std::int64_t frequency) {
        return static_cast<double>(ticks) * 1000.0 / static_cast<double>(frequency);
    };

    line << "gpu frame_ms=";
    if (stats.gpuTimerFreq > 0 && stats.gpuTimeEnd > stats.gpuTimeBegin) {
        line << milliseconds(stats.gpuTimeEnd - stats.gpuTimeBegin, stats.gpuTimerFreq);
    } else {
        line << "unavailable";
    }

    line << " cpu_frame_ms=";
    if (stats.cpuTimerFreq > 0) {
        line << milliseconds(stats.cpuTimeFrame, stats.cpuTimerFreq);
    } else {
        line << "unavailable";
    }
    line << " wait_render_ms=";
    if (stats.cpuTimerFreq > 0) {
        line << milliseconds(stats.waitRender, stats.cpuTimerFreq);
    } else {
        line << "unavailable";
    }
    line << " wait_submit_ms=";
    if (stats.cpuTimerFreq > 0) {
        line << milliseconds(stats.waitSubmit, stats.cpuTimerFreq);
    } else {
        line << "unavailable";
    }

    line << " draws=" << stats.numDraw << " compute=" << stats.numCompute << " views:";
    std::vector<GpuViewTime> views = ReadGpuViewTimes(stats);
    if (views.empty()) {
        line << " none";
    } else {
        std::ranges::stable_sort(views, [](const GpuViewTime& left, const GpuViewTime& right) {
            return left.milliseconds > right.milliseconds;
        });
        for (const GpuViewTime& view : views) {
            line << ' ' << view.name << '#' << view.view << '=' << view.milliseconds;
        }
    }
    return line.str();
}

std::optional<std::string> FrameTimeWindow::Add(std::chrono::steady_clock::time_point frameEnd) {
    if (!start_) {
        start_ = frameEnd;
        last_ = frameEnd;
        frames_ = 0U;
        slowestMilliseconds_ = 0.0;
        return std::nullopt;
    }
    const auto interval = std::chrono::duration<double, std::milli>(frameEnd - last_).count();
    last_ = frameEnd;
    ++frames_;
    slowestMilliseconds_ = std::max(slowestMilliseconds_, interval);
    const auto elapsed = frameEnd - *start_;
    if (elapsed < length_) return std::nullopt;

    const auto elapsedMilliseconds = std::chrono::duration<double, std::milli>(elapsed).count();
    std::ostringstream line;
    line << std::fixed << std::setprecision(2)
         << "frames=" << frames_
         << " mean_ms=" << elapsedMilliseconds / static_cast<double>(frames_)
         << " slowest_ms=" << slowestMilliseconds_;
    start_ = frameEnd;
    frames_ = 0U;
    slowestMilliseconds_ = 0.0;
    return line.str();
}

void FrameTimeWindow::Reset() noexcept {
    start_.reset();
}

} // namespace kb::game
