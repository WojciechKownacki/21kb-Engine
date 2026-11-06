#pragma once

#include "profiler/ProfilerSelection.hpp"

#include <string>
#include <vector>

namespace kb::editor {

// One counter in the scene's list. The model holds numbers and meaning only; the colors, text and
// layout are decided where it is drawn.
struct ProfilerOverlayLine {
    std::string label;
    std::string value;           // formatted with its unit; empty while the counter is unavailable
    bool available = false;
    kb::core::ProfilerSeverity severity = kb::core::ProfilerSeverity::Normal;
};

enum class ProfilerSeriesRole : std::uint8_t { Frame, RenderThread, Gpu };

struct ProfilerGraphSeries {
    ProfilerSeriesRole role = ProfilerSeriesRole::Frame;
    double latest = 0.0;
    std::vector<double> samples;  // milliseconds, oldest first
};

struct ProfilerOverlayModel {
    std::vector<ProfilerOverlayLine> lines;
    std::vector<ProfilerGraphSeries> series;
    double budgetMs = 0.0;   // zero: the line is off
    double alarmMs = 0.0;
    double axisTopMs = 0.0;
    double spanSeconds = 0.0;  // how much time the graph covers

    [[nodiscard]] bool Empty() const noexcept { return lines.empty() && series.empty(); }
};

[[nodiscard]] std::string FormatProfilerValue(const kb::core::ProfilerCounterInfo& info, double value);

// The scene's list holds the selected counters of the measured areas, in registry order; the graph
// holds the selected lines. Each part is empty while the options turn it off.
[[nodiscard]] ProfilerOverlayModel BuildProfilerOverlayModel(
    const kb::core::ProfilerRegistry& registry, const ProfilerSelection& selection);

} // namespace kb::editor
