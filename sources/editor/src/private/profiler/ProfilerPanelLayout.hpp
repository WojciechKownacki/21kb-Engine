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
    AreasAll, AreasNone, CountersAll, CountersNone, Area,
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

// A row of the budget table: a counter that speaks for an area. The first row of an area carries
// the switch that turns its measuring on and off.
struct ProfilerBudgetRow {
    kb::core::ProfilerArea area = kb::core::ProfilerArea::Cpu;
    kb::core::ProfilerBuiltIn counter = kb::core::ProfilerBuiltIn::FrameTime;
    bool startsArea = false;
    RECT bounds{};
};

// Where everything is, for a panel of a given size and a given choice of areas. Painting and
// hit-testing both read it, so what is drawn is what is clicked.
struct ProfilerPanelGeometry {
    RECT content{};
    RECT tabs{}, toolbar{}, status{};
    // The overview under the toolbar: the verdict in plain words and the big numbers. One line
    // high when the panel is short, empty when there is no room for it at all.
    RECT health{};
    bool healthCompact = false;
    RECT left{}, center{}, right{};
    // The left pane of Counters: the areas to measure, and a word on what switching one on does.
    RECT areasHeader{}, areasList{}, areasHint{};
    // Measurements: the frames as bars, the budget table, and the regions of the shown frame.
    // The strip and the hierarchy are empty in a panel too low for them.
    RECT frameStrip{}, framePlot{}, budgetHeader{};
    std::vector<ProfilerBudgetRow> budgetRows;
    RECT hierarchyTabs{}, hierarchyHeader{}, hierarchyRows{};
    // Counters and Diagnosis share a summary strip, a column header and a list of rows.
    RECT summary{}, tableHeader{}, tableRows{};

    static constexpr int kRowHeight = 22;
    static constexpr int kCounterRowHeight = 24;
    // A headline and two lines of what it means.
    static constexpr int kFindingHeight = 58;
    static constexpr int kLeftWidth = 220;
    static constexpr int kHealthHeight = 60;
    static constexpr int kHealthCompactHeight = 30;
    static constexpr int kRightWidth = 272;
    static constexpr int kBudgetRowHeight = 28;
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

// The x positions of the columns of the budget table: the name, the last frames as a line, the
// reading now and on average, how much of the budget it takes, and the switch of the area.
struct ProfilerBudgetColumns {
    int name = 0, spark = 0, now = 0, average = 0, budget = 0, toggle = 0, end = 0;
};
[[nodiscard]] ProfilerBudgetColumns ResolveProfilerBudgetColumns(const RECT& row);
// Where the switch of a row of the budget table is.
[[nodiscard]] RECT ProfilerBudgetSwitchRect(const RECT& row);

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
