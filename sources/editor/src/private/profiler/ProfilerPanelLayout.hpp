#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "profiler/EditorProfilerState.hpp"

#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

// Everything in the Profiler panel that answers a click.
enum class ProfilerPanelAction : std::uint8_t {
    Tab, Record, Pause, Clear, FirstFrame, PreviousFrame, NextFrame, Live, Snapshot, WhatToShow,
    AreasAll, AreasNone, CountersAll, CountersNone, Area, LeftCounter,
    PickFrame,
    CounterPanel, CounterScene, CounterBudget, CounterAlarm,
    Severity, Category, ShowIgnored, Finding, ShowInMeasurements, IgnoreFinding,
};

struct ProfilerPanelHit {
    ProfilerPanelAction action = ProfilerPanelAction::Tab;
    int argument = 0;
    RECT bounds{};
};

// A button of the strips: its words, and whether it is lit or cannot be pressed.
struct ProfilerPanelButton {
    ProfilerPanelHit hit;
    std::string text;
    bool active = false;
    bool enabled = true;
};

enum class ProfilerChartKind : std::uint8_t { Frame, Cpu, Gpu, Memory, Drawing };

struct ProfilerChartRow {
    ProfilerChartKind kind = ProfilerChartKind::Frame;
    RECT row{};
    RECT label{};
    RECT plot{};
};

// Where everything is, for a panel of a given size and a given choice of areas. Painting and
// hit-testing both read it, so what is drawn is what is clicked.
struct ProfilerPanelGeometry {
    RECT content{};
    RECT tabs{}, toolbar{}, status{};
    RECT left{}, center{}, right{};
    // The left pane of the first two tabs: the areas, then the counters.
    RECT areasHeader{}, areasList{}, countersHeader{}, countersList{};
    // Measurements.
    RECT ruler{}, hierarchyTabs{}, hierarchyHeader{}, hierarchyRows{};
    std::vector<ProfilerChartRow> charts;
    // Counters and Diagnosis share a summary strip, a column header and a list of rows.
    RECT summary{}, tableHeader{}, tableRows{};

    static constexpr int kRowHeight = 22;
    static constexpr int kCounterRowHeight = 24;
    static constexpr int kFindingHeight = 48;
    static constexpr int kLeftWidth = 250;
    static constexpr int kRightWidth = 272;
    static constexpr int kLabelWidth = 170;
};

[[nodiscard]] ProfilerPanelGeometry ResolveProfilerPanelGeometry(const RECT& content, const ProfilerPanelState& state, const ProfilerSelection& selection);

// The buttons of the tab strip, the toolbar and the "all / none" links, in drawing order.
[[nodiscard]] std::vector<ProfilerPanelButton> ProfilerPanelButtons(const ProfilerPanelGeometry& geometry,
    const EditorProfilerState& profiler, const ProfilerSelection& selection);

// The x positions of the columns of the counters table, from the left edge of the first to the
// right edge of the last.
struct ProfilerCounterColumns {
    int check = 0, name = 0, now = 0, average = 0, minimum = 0, maximum = 0, p95 = 0, budget = 0, alarm = 0, spark = 0, scene = 0, end = 0;
};
[[nodiscard]] ProfilerCounterColumns ResolveProfilerCounterColumns(const RECT& rows);

// The counters of a table or list, in registry order, with the group each starts.
struct ProfilerCounterRow {
    std::size_t counter = 0U;
    bool startsGroup = false;
};
[[nodiscard]] std::vector<ProfilerCounterRow> ProfilerCounterRows(const kb::core::ProfilerRegistry& registry);

// The findings the panel lists: those the filters let through.
[[nodiscard]] std::vector<const kb::core::DiagnosisFinding*> ProfilerVisibleFindings(const EditorProfilerState& profiler);
// The finding the details show: the picked one, if it is still listed; nullptr when none.
[[nodiscard]] const kb::core::DiagnosisFinding* ProfilerSelectedFinding(const EditorProfilerState& profiler);

// The hits of the current tab: buttons, rows, cells and chart areas.
[[nodiscard]] std::vector<ProfilerPanelHit> ProfilerPanelHits(const ProfilerPanelGeometry& geometry,
    const EditorProfilerState& profiler, const ProfilerSelection& selection);

// The frame a chart column stands for: the offset from the newest reading, and the other way round.
[[nodiscard]] int ProfilerFrameOffsetAt(const RECT& plot, int x, std::size_t samples);
[[nodiscard]] int ProfilerFrameX(const RECT& plot, std::size_t offsetFromNewest);

// The oldest frame the charts still hold: the frames are numbered as they are sampled, but the
// charts and the regions hold only what was measured since the measurements were last cleared or an
// area was last switched on, so the newest readings are the newest frames and no more.
[[nodiscard]] std::uint64_t ProfilerOldestFrame(const EditorProfilerState& profiler);
// The number of the frame the panel shows: the selected one while it is still held, else the
// latest. Empty before any frame.
[[nodiscard]] std::optional<std::uint64_t> ProfilerShownFrame(const EditorProfilerState& profiler);

#endif

} // namespace kb::editor
