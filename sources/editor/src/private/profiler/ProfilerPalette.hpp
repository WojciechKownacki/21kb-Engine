#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "profiler/ProfilerOverlayModel.hpp"

namespace kb::editor::profiler_palette {

// The theme has no status colors, and the graph needs lines that stay apart on any scene, so the
// profiler keeps its few meaning colors in one place. Everything else comes from EditorTheme.
inline constexpr EditorColor kWarning{ 242, 166, 64 };
inline constexpr EditorColor kAlarm{ 235, 92, 92 };
inline constexpr EditorColor kBudget{ 242, 191, 51 };
inline constexpr EditorColor kBlue{ 82, 158, 242 };
inline constexpr EditorColor kGreen{ 92, 199, 128 };
inline constexpr EditorColor kPurple{ 167, 139, 250 };
inline constexpr EditorColor kCyan{ 77, 196, 216 };

[[nodiscard]] inline EditorColor Severity(const EditorTheme& theme, kb::core::ProfilerSeverity severity) noexcept {
    switch (severity) {
    case kb::core::ProfilerSeverity::Warning: return kWarning;
    case kb::core::ProfilerSeverity::Alarm: return kAlarm;
    case kb::core::ProfilerSeverity::Normal: break;
    }
    return theme.textPrimary;
}

[[nodiscard]] inline EditorColor Series(const EditorTheme& theme, ProfilerSeriesRole role) noexcept {
    switch (role) {
    case ProfilerSeriesRole::RenderThread: return kGreen;
    case ProfilerSeriesRole::Gpu: return kWarning;
    case ProfilerSeriesRole::Frame: break;
    }
    return theme.textPrimary;
}

// The color of an area, wherever it stands for one: five hues that stay apart from one another and
// from the warning and the alarm, so a color over a budget is never mistaken for an area.
[[nodiscard]] inline EditorColor Area(kb::core::ProfilerArea area) noexcept {
    switch (area) {
    case kb::core::ProfilerArea::Cpu: return EditorColor{ 57, 135, 229 };
    case kb::core::ProfilerArea::Gpu: return EditorColor{ 217, 89, 38 };
    case kb::core::ProfilerArea::Render: return EditorColor{ 25, 158, 112 };
    case kb::core::ProfilerArea::Ecs: return EditorColor{ 201, 133, 0 };
    case kb::core::ProfilerArea::Memory: return EditorColor{ 213, 81, 129 };
    }
    return kBlue;
}

} // namespace kb::editor::profiler_palette
