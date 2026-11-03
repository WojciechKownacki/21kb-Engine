#include "profiler/ProfilerPanelLayout.hpp"

#if defined(_WIN32)
#include "localization/EditorLocalization.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace kb::editor {
namespace {

using kb::core::ProfilerArea;
using kb::core::ProfilerRegistry;

constexpr int kTabsHeight = 34;
constexpr int kToolbarHeight = 40;
constexpr int kStatusHeight = 22;
constexpr int kHeaderHeight = 28;
constexpr int kRulerHeight = 26;
constexpr int kSummaryHeight = 34;
constexpr int kDiagnosisSummaryHeight = 56;
constexpr int kTableHeaderHeight = 22;
constexpr int kButtonHeight = 26;
constexpr int kGap = 8;
constexpr std::array<ProfilerArea, 5> kAreas{
    ProfilerArea::Cpu, ProfilerArea::Gpu, ProfilerArea::Render, ProfilerArea::Ecs, ProfilerArea::Memory };

[[nodiscard]] RECT Rect(int left, int top, int right, int bottom) noexcept { return RECT{ left, top, right, bottom }; }

// A button is as wide as its words, as the Latin and the wider scripts both need.
[[nodiscard]] int ButtonWidth(const std::string& text, int padding) noexcept {
    return padding + static_cast<int>(text.size()) * 7;
}

} // namespace

ProfilerPanelGeometry ResolveProfilerPanelGeometry(const RECT& content, const ProfilerPanelState& state, const ProfilerSelection& selection) {
    ProfilerPanelGeometry geometry;
    geometry.content = content;
    const int width = content.right - content.left;
    const int leftWidth = width >= 760 ? ProfilerPanelGeometry::kLeftWidth : 200;
    const int rightWidth = state.tab == ProfilerPanelTab::Diagnosis && width >= 900 ? ProfilerPanelGeometry::kRightWidth : 0;
    geometry.tabs = Rect(content.left, content.top, content.right, content.top + kTabsHeight);
    geometry.toolbar = Rect(content.left, geometry.tabs.bottom, content.right, geometry.tabs.bottom + kToolbarHeight);
    geometry.status = Rect(content.left, content.bottom - kStatusHeight, content.right, content.bottom);
    const int bodyTop = geometry.toolbar.bottom;
    const int bodyBottom = geometry.status.top;
    geometry.left = Rect(content.left, bodyTop, content.left + leftWidth, bodyBottom);
    geometry.right = Rect(content.right - rightWidth, bodyTop, content.right, bodyBottom);
    geometry.center = Rect(geometry.left.right, bodyTop, geometry.right.left, bodyBottom);

    // The left pane: the five areas, then the counters under their own header.
    const int rows = ProfilerPanelGeometry::kRowHeight;
    geometry.areasHeader = Rect(geometry.left.left, bodyTop, geometry.left.right, bodyTop + kHeaderHeight);
    geometry.areasList = Rect(geometry.left.left, geometry.areasHeader.bottom, geometry.left.right, geometry.areasHeader.bottom + static_cast<int>(kAreas.size()) * rows);
    geometry.countersHeader = Rect(geometry.left.left, geometry.areasList.bottom + kGap, geometry.left.right, geometry.areasList.bottom + kGap + kHeaderHeight);
    geometry.countersList = Rect(geometry.left.left, geometry.countersHeader.bottom, geometry.left.right, std::max<int>(geometry.countersHeader.bottom, bodyBottom));

    if (state.tab == ProfilerPanelTab::Measurements) {
        geometry.ruler = Rect(geometry.center.left, bodyTop, geometry.center.right, bodyTop + kRulerHeight);
        struct Row { ProfilerChartKind kind; int height; };
        std::vector<Row> wanted;
        if (selection.AreaMeasured(ProfilerArea::Cpu)) {
            wanted.push_back({ ProfilerChartKind::Frame, 64 });
            wanted.push_back({ ProfilerChartKind::Cpu, 64 });
        }
        if (selection.AreaMeasured(ProfilerArea::Gpu)) wanted.push_back({ ProfilerChartKind::Gpu, 56 });
        if (selection.AreaMeasured(ProfilerArea::Memory)) wanted.push_back({ ProfilerChartKind::Memory, 56 });
        if (selection.AreaMeasured(ProfilerArea::Render)) wanted.push_back({ ProfilerChartKind::Drawing, 56 });
        int total = 0;
        for (const Row& row : wanted) total += row.height;
        // The charts give way before the hierarchy below them does.
        constexpr int kHierarchyMinimum = 150;
        const int available = bodyBottom - geometry.ruler.bottom - kHierarchyMinimum;
        const double scale = total <= 0 ? 1.0 : std::clamp(static_cast<double>(available) / total, 0.5, 1.0);
        int top = geometry.ruler.bottom;
        for (const Row& row : wanted) {
            const int height = static_cast<int>(row.height * scale);
            ProfilerChartRow chart;
            chart.kind = row.kind;
            chart.row = Rect(geometry.center.left, top, geometry.center.right, top + height);
            chart.label = Rect(chart.row.left, top, chart.row.left + ProfilerPanelGeometry::kLabelWidth, top + height);
            chart.plot = Rect(chart.label.right, top, chart.row.right, top + height);
            geometry.charts.push_back(chart);
            top += height;
        }
        geometry.hierarchyTabs = Rect(geometry.center.left, top, geometry.center.right, top + kHeaderHeight);
        geometry.hierarchyHeader = Rect(geometry.center.left, geometry.hierarchyTabs.bottom, geometry.center.right, geometry.hierarchyTabs.bottom + kTableHeaderHeight);
        geometry.hierarchyRows = Rect(geometry.center.left, geometry.hierarchyHeader.bottom, geometry.center.right, std::max<int>(geometry.hierarchyHeader.bottom, bodyBottom));
    } else {
        const int summary = state.tab == ProfilerPanelTab::Counters ? kSummaryHeight : kDiagnosisSummaryHeight;
        geometry.summary = Rect(geometry.center.left, bodyTop, geometry.center.right, bodyTop + summary);
        geometry.tableHeader = Rect(geometry.center.left, geometry.summary.bottom, geometry.center.right, geometry.summary.bottom + kTableHeaderHeight);
        geometry.tableRows = Rect(geometry.center.left, geometry.tableHeader.bottom, geometry.center.right, std::max<int>(geometry.tableHeader.bottom, bodyBottom));
    }
    return geometry;
}

std::vector<ProfilerPanelButton> ProfilerPanelButtons(const ProfilerPanelGeometry& geometry, const EditorProfilerState& profiler, const ProfilerSelection&) {
    using Action = ProfilerPanelAction;
    const ProfilerPanelState& state = profiler.Panel();
    std::vector<ProfilerPanelButton> buttons;
    const auto add = [&buttons](Action action, int argument, const RECT& bounds, std::string text, bool active = false, bool enabled = true) {
        buttons.push_back({ { action, argument, bounds }, std::move(text), active, enabled });
    };

    // Tabs.
    const std::array<std::string, 3> tabs{ std::string{ EditorText("profiler.panel.tab.measurements", "Measurements") }, std::string{ EditorText("profiler.panel.tab.counters", "Counters") }, std::string{ EditorText("profiler.panel.tab.diagnosis", "Diagnosis") } };
    int tabLeft = geometry.tabs.left + 8;
    for (std::size_t index = 0U; index < tabs.size(); ++index) {
        // The diagnosis tab leaves room for the number of findings it carries.
        const int width = ButtonWidth(tabs[index], 28) + (index == 2U ? 28 : 0);
        add(Action::Tab, static_cast<int>(index), Rect(tabLeft, geometry.tabs.bottom - 30, tabLeft + width, geometry.tabs.bottom), tabs[index], state.tab == static_cast<ProfilerPanelTab>(index));
        tabLeft += width + 2;
    }

    // Toolbar, left to right: recording, pausing and clearing; the frame; then the two buttons at the right.
    const int top = geometry.toolbar.top + (geometry.toolbar.bottom - geometry.toolbar.top - kButtonHeight) / 2;
    int cursor = geometry.toolbar.left + 10;
    const auto place = [&](Action action, std::string text, bool active, int padding = 26) {
        const int width = ButtonWidth(text, padding);
        add(action, 0, Rect(cursor, top, cursor + width, top + kButtonHeight), std::move(text), active);
        cursor += width + kGap;
    };
    const kb::core::ProfilerRecorder& recorder = profiler.Recorder();
    place(Action::Record, recorder.Recording()
            ? std::format("{} · {}", std::string{ EditorText("profiler.panel.stop", "Stop") }, recorder.Frames())
            : std::string{ EditorText("profiler.panel.record", "Record") }, recorder.Recording(), 52);
    place(Action::Pause, state.paused ? std::string{ EditorText("profiler.panel.resume", "Resume") } : std::string{ EditorText("profiler.panel.pause", "Pause") }, state.paused);
    place(Action::Clear, std::string{ EditorText("profiler.panel.clear", "Clear") }, false);
    cursor += kGap;
    constexpr int kNavigation = 28;
    const std::array<std::pair<Action, int>, 2> before{ { { Action::FirstFrame, 0 }, { Action::PreviousFrame, 0 } } };
    const std::array<std::string, 4> arrows{ "|<", "<", ">", ">|" };
    for (std::size_t index = 0U; index < 2U; ++index) {
        add(before[index].first, 0, Rect(cursor, top, cursor + kNavigation, top + kButtonHeight), arrows[index]);
        cursor += kNavigation + 4;
    }
    // The frame label sits between the arrows; it is drawn by the renderer and takes this much room.
    cursor += 118;
    add(Action::NextFrame, 0, Rect(cursor, top, cursor + kNavigation, top + kButtonHeight), arrows[2]);
    cursor += kNavigation + 4;
    add(Action::Live, 1, Rect(cursor, top, cursor + kNavigation, top + kButtonHeight), arrows[3]);
    cursor += kNavigation + kGap;
    const bool following = !state.selectedFrame.has_value();
    place(Action::Live, std::string{ EditorText("profiler.panel.live", "Live") }, following, 24);
    int right = geometry.toolbar.right - 10;
    const std::array<std::pair<Action, std::string>, 2> end{ { { Action::WhatToShow, std::string{ EditorText("profiler.panel.what_to_show", "What to show...") } },
        { Action::Snapshot, std::string{ EditorText("profiler.panel.snapshot", "Frame snapshot") } } } };
    for (const auto& [action, text] : end) {
        const int width = ButtonWidth(text, 26);
        add(action, 0, Rect(right - width, top, right, top + kButtonHeight), text, action == Action::WhatToShow && profiler.WindowOpen());
        right -= width + kGap;
    }

    // The links "all · none" in the headers of the lists.
    const auto links = [&](const RECT& header, Action all, Action none) {
        const std::string noneText = std::string{ EditorText("profiler.select.none", "none") };
        const std::string allText = std::string{ EditorText("profiler.select.all", "all") };
        const int noneWidth = ButtonWidth(noneText, 4);
        const int allWidth = ButtonWidth(allText, 4);
        const RECT noneRect = Rect(header.right - 10 - noneWidth, header.top + 5, header.right - 10, header.bottom - 5);
        add(none, 0, noneRect, noneText);
        add(all, 0, Rect(noneRect.left - 14 - allWidth, header.top + 5, noneRect.left - 14, header.bottom - 5), allText);
    };
    if (state.tab != ProfilerPanelTab::Diagnosis) {
        links(geometry.areasHeader, Action::AreasAll, Action::AreasNone);
        links(geometry.countersHeader, Action::CountersAll, Action::CountersNone);
    }
    return buttons;
}

std::vector<ProfilerCounterRow> ProfilerCounterRows(const ProfilerRegistry& registry) {
    std::vector<ProfilerCounterRow> rows;
    std::string_view group;
    for (std::size_t counter = 0U; counter < registry.CounterCount(); ++counter) {
        const std::string_view counterGroup = registry.Info(counter).group;
        rows.push_back({ counter, counterGroup != group });
        group = counterGroup;
    }
    return rows;
}

std::vector<const kb::core::DiagnosisFinding*> ProfilerVisibleFindings(const EditorProfilerState& profiler) {
    const ProfilerPanelState& state = profiler.Panel();
    std::vector<const kb::core::DiagnosisFinding*> visible;
    for (const kb::core::DiagnosisFinding& finding : profiler.Findings()) {
        if (state.ignored.contains(finding.Key()) && !state.showIgnored) continue;
        if (!state.showSeverity[static_cast<std::size_t>(finding.severity)] || !state.showCategory[static_cast<std::size_t>(finding.category)]) continue;
        visible.push_back(&finding);
    }
    return visible;
}

const kb::core::DiagnosisFinding* ProfilerSelectedFinding(const EditorProfilerState& profiler) {
    const std::string& key = profiler.Panel().selectedFinding;
    for (const kb::core::DiagnosisFinding* finding : ProfilerVisibleFindings(profiler)) {
        if (!key.empty() && finding->Key() == key) return finding;
    }
    return nullptr;
}

ProfilerCounterColumns ResolveProfilerCounterColumns(const RECT& rows) {
    ProfilerCounterColumns columns;
    columns.end = rows.right - 12;
    columns.scene = columns.end - 48;
    columns.spark = columns.scene - 80;
    columns.alarm = columns.spark - 70;
    columns.budget = columns.alarm - 70;
    columns.p95 = columns.budget - 60;
    columns.maximum = columns.p95 - 60;
    columns.minimum = columns.maximum - 60;
    columns.average = columns.minimum - 60;
    columns.now = columns.average - 72;
    columns.name = rows.left + 12 + 24;
    columns.check = rows.left + 12;
    return columns;
}

int ProfilerFrameOffsetAt(const RECT& plot, int x, std::size_t samples) {
    const double step = static_cast<double>(plot.right - plot.left) / static_cast<double>(kb::core::ProfilerRegistry::HistoryFrames() - 1U);
    const int offset = static_cast<int>(std::lround((plot.right - x) / step));
    return std::clamp(offset, 0, static_cast<int>(samples) - 1);
}

int ProfilerFrameX(const RECT& plot, std::size_t offsetFromNewest) {
    const double step = static_cast<double>(plot.right - plot.left) / static_cast<double>(kb::core::ProfilerRegistry::HistoryFrames() - 1U);
    return static_cast<int>(std::lround(plot.right - static_cast<double>(offsetFromNewest) * step));
}

std::uint64_t ProfilerOldestFrame(const EditorProfilerState& profiler) {
    const std::uint64_t latest = profiler.Frame();
    const std::uint64_t held = std::min<std::uint64_t>(profiler.Registry().SampleCount(ProfilerRegistry::Id(kb::core::ProfilerBuiltIn::FrameTime)), latest);
    return held == 0U ? latest : latest - held + 1U;
}

std::optional<std::uint64_t> ProfilerShownFrame(const EditorProfilerState& profiler) {
    const std::uint64_t latest = profiler.Frame();
    if (latest == 0U) return std::nullopt;
    const std::optional<std::uint64_t>& selected = profiler.Panel().selectedFrame;
    return selected && *selected >= ProfilerOldestFrame(profiler) && *selected <= latest ? *selected : latest;
}

std::vector<ProfilerPanelHit> ProfilerPanelHits(const ProfilerPanelGeometry& geometry, const EditorProfilerState& profiler, const ProfilerSelection& selection) {
    using Action = ProfilerPanelAction;
    const ProfilerPanelState& state = profiler.Panel();
    std::vector<ProfilerPanelHit> hits;
    for (const ProfilerPanelButton& button : ProfilerPanelButtons(geometry, profiler, selection)) {
        if (button.enabled) hits.push_back(button.hit);
    }
    const auto inside = [](const RECT& list, int top, int bottom) { return bottom > list.top && top < list.bottom; };

    if (state.tab == ProfilerPanelTab::Diagnosis) {
        // Filters at the left, then the findings, then the buttons of the details.
        int y = geometry.left.top + kHeaderHeight;
        for (int severity = 0; severity < 3; ++severity, y += ProfilerPanelGeometry::kRowHeight) {
            hits.push_back({ Action::Severity, severity, Rect(geometry.left.left, y, geometry.left.right, y + ProfilerPanelGeometry::kRowHeight) });
        }
        y += kHeaderHeight;
        for (int category = 0; category < 4; ++category, y += ProfilerPanelGeometry::kRowHeight) {
            hits.push_back({ Action::Category, category, Rect(geometry.left.left, y, geometry.left.right, y + ProfilerPanelGeometry::kRowHeight) });
        }
        y += kHeaderHeight;
        hits.push_back({ Action::ShowIgnored, 0, Rect(geometry.left.left, y, geometry.left.right, y + ProfilerPanelGeometry::kRowHeight) });
        constexpr int kFindingHeight = ProfilerPanelGeometry::kFindingHeight;
        const std::size_t count = ProfilerVisibleFindings(profiler).size();
        for (std::size_t index = 0U; index < count; ++index) {
            const int top = geometry.tableRows.top + static_cast<int>(index) * kFindingHeight - state.findingsScroll;
            if (inside(geometry.tableRows, top, top + kFindingHeight)) {
                hits.push_back({ Action::Finding, static_cast<int>(index), Rect(geometry.tableRows.left, std::max<int>(top, geometry.tableRows.top), geometry.tableRows.right, std::min<int>(top + kFindingHeight, geometry.tableRows.bottom)) });
            }
        }
        if (geometry.right.right > geometry.right.left) {
            const int buttonTop = geometry.right.bottom - 40;
            hits.push_back({ Action::ShowInMeasurements, 0, Rect(geometry.right.left + 12, buttonTop, geometry.right.left + 12 + 168, buttonTop + kButtonHeight) });
            hits.push_back({ Action::IgnoreFinding, 0, Rect(geometry.right.left + 12 + 168 + kGap, buttonTop, geometry.right.right - 12, buttonTop + kButtonHeight) });
        }
        return hits;
    }

    const kb::core::ProfilerRegistry& registry = profiler.Registry();
    const int rowHeight = ProfilerPanelGeometry::kRowHeight;
    for (std::size_t index = 0U; index < kAreas.size(); ++index) {
        const int top = geometry.areasList.top + static_cast<int>(index) * rowHeight;
        hits.push_back({ Action::Area, static_cast<int>(kAreas[index]), Rect(geometry.areasList.left, top, geometry.areasList.right, top + rowHeight) });
    }
    int top = geometry.countersList.top - state.leftScroll;
    for (const ProfilerCounterRow& row : ProfilerCounterRows(registry)) {
        if (row.startsGroup) top += rowHeight;
        if (inside(geometry.countersList, top, top + rowHeight)) {
            hits.push_back({ Action::LeftCounter, static_cast<int>(row.counter), Rect(geometry.countersList.left, std::max<int>(top, geometry.countersList.top), geometry.countersList.right, std::min<int>(top + rowHeight, geometry.countersList.bottom)) });
        }
        top += rowHeight;
    }

    if (state.tab == ProfilerPanelTab::Measurements) {
        for (std::size_t index = 0U; index < geometry.charts.size(); ++index) hits.push_back({ Action::PickFrame, static_cast<int>(index), geometry.charts[index].plot });
        return hits;
    }
    // Counters: the cells that answer a click, row by row.
    const ProfilerCounterColumns columns = ResolveProfilerCounterColumns(geometry.tableRows);
    int y = geometry.tableRows.top - state.countersScroll;
    for (const ProfilerCounterRow& row : ProfilerCounterRows(registry)) {
        if (row.startsGroup) y += rowHeight;
        if (inside(geometry.tableRows, y, y + ProfilerPanelGeometry::kCounterRowHeight)) {
            const int rowTop = std::max<int>(y, geometry.tableRows.top);
            const int rowBottom = std::min<int>(y + ProfilerPanelGeometry::kCounterRowHeight, geometry.tableRows.bottom);
            const int counter = static_cast<int>(row.counter);
            hits.push_back({ Action::CounterPanel, counter, Rect(columns.check - 4, rowTop, columns.check + 20, rowBottom) });
            hits.push_back({ Action::CounterBudget, counter, Rect(columns.budget, rowTop, columns.alarm, rowBottom) });
            hits.push_back({ Action::CounterAlarm, counter, Rect(columns.alarm, rowTop, columns.spark, rowBottom) });
            hits.push_back({ Action::CounterScene, counter, Rect(columns.scene, rowTop, columns.end, rowBottom) });
        }
        y += ProfilerPanelGeometry::kCounterRowHeight;
    }
    return hits;
}

} // namespace kb::editor

#endif
