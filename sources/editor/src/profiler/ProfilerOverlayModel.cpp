#include "profiler/ProfilerOverlayModel.hpp"
#include "profiler/ProfilerNumber.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numeric>

namespace kb::editor {
namespace {

using kb::core::ProfilerBuiltIn;
using kb::core::ProfilerRegistry;
using kb::core::ProfilerUnit;

struct SeriesSource {
    ProfilerSeriesRole role;
    ProfilerBuiltIn counter;
    bool ProfilerGraphLines::* enabled;
};

constexpr std::array<SeriesSource, 3> kSeries{{
    { ProfilerSeriesRole::Frame, ProfilerBuiltIn::FrameTime, &ProfilerGraphLines::frame },
    { ProfilerSeriesRole::RenderThread, ProfilerBuiltIn::RenderThreadTime, &ProfilerGraphLines::renderThread },
    { ProfilerSeriesRole::Gpu, ProfilerBuiltIn::GpuTime, &ProfilerGraphLines::gpu },
}};

[[nodiscard]] bool Measured(const ProfilerSelection& selection, const kb::core::ProfilerCounterInfo& info) {
    return selection.AreaMeasured(info.area);
}

} // namespace

std::string FormatProfilerValue(const kb::core::ProfilerCounterInfo& info, double value) {
    switch (info.unit) {
    case ProfilerUnit::Milliseconds: return ProfilerNumber::Fixed(value, 1) + " ms";
    case ProfilerUnit::FramesPerSecond: return ProfilerNumber::Fixed(value, 1);
    case ProfilerUnit::Percent: return ProfilerNumber::Fixed(value, 1) + "%";
    case ProfilerUnit::Megabytes:
        return value >= 1024.0 ? ProfilerNumber::Fixed(value / 1024.0, 2) + " GB" : ProfilerNumber::Fixed(value, 0) + " MB";
    case ProfilerUnit::Count:
        return value >= 1'000'000.0 ? ProfilerNumber::Fixed(value / 1'000'000.0, 1) + " M" : ProfilerNumber::Fixed(value, 0);
    }
    return {};
}

ProfilerOverlayModel BuildProfilerOverlayModel(
    const ProfilerRegistry& registry, const ProfilerSelection& selection) {
    ProfilerOverlayModel model;
    const ProfilerOverlayOptions& options = selection.overlay;

    if (options.listVisible) {
        for (std::size_t counter = 0U; counter < registry.CounterCount(); ++counter) {
            const kb::core::ProfilerCounterInfo& info = registry.Info(counter);
            if (!selection.OnScene(info.id) || !Measured(selection, info)) continue;
            const kb::core::ProfilerCounterStats stats = registry.Stats(counter);
            ProfilerOverlayLine line{ .label = std::string{ info.shortLabel }, .available = stats.available };
            if (stats.available) {
                line.value = FormatProfilerValue(info, stats.now);
                line.severity = ProfilerRegistry::Severity(info, stats.now);
            }
            model.lines.push_back(std::move(line));
        }
    }

    if (options.graphVisible) {
        const kb::core::ProfilerCounterInfo& frameInfo = registry.Info(ProfilerRegistry::Id(ProfilerBuiltIn::FrameTime));
        double highest = 0.0;
        for (const SeriesSource& source : kSeries) {
            const std::size_t counter = ProfilerRegistry::Id(source.counter);
            if (!(options.lines.*source.enabled) || !Measured(selection, registry.Info(counter))) continue;
            std::array<double, ProfilerRegistry::HistoryFrames()> samples{};
            const std::size_t count = registry.Samples(counter, samples);
            if (count == 0U) continue;
            ProfilerGraphSeries series{ .role = source.role, .latest = samples[count - 1U] };
            series.samples.assign(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(count));
            highest = std::max(highest, *std::ranges::max_element(series.samples));
            model.series.push_back(std::move(series));
        }
        if (options.lines.budget) model.budgetMs = frameInfo.budget;
        if (options.lines.alarm) model.alarmMs = frameInfo.alarm;
        // Room above the highest line, in whole 5 ms steps, so the thresholds and the readings fit.
        const double top = std::max({ model.alarmMs * 1.2, model.budgetMs * 1.2, highest * 1.1, 5.0 });
        model.axisTopMs = std::ceil(top / 5.0) * 5.0;
        // The history is a count of frames; the time it covers is what those frames cost.
        if (selection.AreaMeasured(kb::core::ProfilerArea::Cpu)) {
            std::array<double, ProfilerRegistry::HistoryFrames()> frames{};
            const std::size_t frameCount = registry.Samples(ProfilerRegistry::Id(ProfilerBuiltIn::FrameTime), frames);
            model.spanSeconds = std::accumulate(frames.begin(), frames.begin() + static_cast<std::ptrdiff_t>(frameCount), 0.0) / 1000.0;
        }
    }
    return model;
}

} // namespace kb::editor
