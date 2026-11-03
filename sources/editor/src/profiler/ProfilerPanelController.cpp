#include "profiler/ProfilerPanel.hpp"

#if defined(_WIN32)
#include "localization/EditorLocalization.hpp"
#include "platform/win32/EditorTextEntryDialog.hpp"
#include "profiler/ProfilerFiles.hpp"
#include "profiler/ProfilerPanelLayout.hpp"
#include "profiler/ProfilerText.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace kb::editor {
namespace {

using Action = ProfilerPanelAction;
using kb::core::ProfilerArea;
using kb::core::ProfilerBuiltIn;
using kb::core::ProfilerRegistry;

constexpr std::array<ProfilerArea, 5> kAreas{
    ProfilerArea::Cpu, ProfilerArea::Gpu, ProfilerArea::Render, ProfilerArea::Ecs, ProfilerArea::Memory };
constexpr int kFindingHeight = ProfilerPanelGeometry::kFindingHeight;

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

// The choice is stored with the project's editor settings, and the scene follows it at once.
void Commit(EditorSceneContext& context, const ProfilerSelection& selection) {
    EditorConfiguration configuration = context.EditorConfig();
    configuration.profiler = selection;
    if (!context.SaveEditorConfig(std::move(configuration))) return;
    context.Profiler().RefreshOverlay(selection, std::chrono::steady_clock::now(), true);
}

// A threshold typed as a number, with a comma or a point; empty is none.
[[nodiscard]] std::optional<double> ParseThreshold(std::string text) {
    std::ranges::replace(text, ',', '.');
    while (!text.empty() && text.back() == ' ') text.pop_back();
    while (!text.empty() && text.front() == ' ') text.erase(text.begin());
    if (text.empty()) return 0.0;
    double value = 0.0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(value) || value < 0.0) return std::nullopt;
    return value;
}

void EditThreshold(HWND owner, EditorSceneContext& context, std::size_t counter, bool alarm) {
    EditorProfilerState& profiler = context.Profiler();
    const kb::core::ProfilerCounterInfo& info = profiler.Registry().Info(counter);
    const double current = alarm ? info.alarm : info.budget;
    const std::optional<std::string> text = EditorTextEntryDialog::Show(owner, EditorTextEntryDialogDescriptor{
        .title = std::string{ ProfilerText::Counter(info) },
        .label = std::string{ alarm ? EditorText("profiler.panel.alarm_label", "Alarm") : EditorText("profiler.panel.budget_label", "Budget") },
        .value = current > 0.0 ? std::format("{:g}", current) : std::string{},
        .hint = std::string{ EditorText("profiler.panel.threshold_hint", "A number; empty for none. Clear both to restore the built-in thresholds.") },
    });
    if (!text) return;
    const std::optional<double> value = ParseThreshold(*text);
    if (!value) {
        profiler.Panel().lastSaved = std::string{ EditorText("profiler.panel.not_a_number", "A threshold is a number of zero or more.") };
        return;
    }
    ProfilerSelection selection = context.EditorConfig().profiler;
    selection.SetThreshold(info.id, alarm ? info.budget : *value, alarm ? *value : info.alarm);
    Commit(context, selection);
}

[[nodiscard]] std::uint64_t OldestFrame(const EditorProfilerState& profiler) {
    const std::uint64_t latest = profiler.Frame();
    return latest > ProfilerRegistry::HistoryFrames() ? latest - ProfilerRegistry::HistoryFrames() + 1U : 1U;
}

void StartOrStopRecording(EditorSceneContext& context) {
    EditorProfilerState& profiler = context.Profiler();
    ProfilerPanelState& panel = profiler.Panel();
    if (profiler.Recorder().Recording()) {
        profiler.Recorder().Stop();
        panel.lastSaved = ProfilerFiles::SaveRecording(profiler);
        return;
    }
    // The counters of the panel's list that are measured.
    const ProfilerSelection& selection = context.EditorConfig().profiler;
    std::vector<std::size_t> counters;
    for (std::size_t counter = 0U; counter < profiler.Registry().CounterCount(); ++counter) {
        const kb::core::ProfilerCounterInfo& info = profiler.Registry().Info(counter);
        if (selection.InPanel(info.id) && selection.AreaMeasured(info.area)) counters.push_back(counter);
    }
    if (counters.empty()) {
        panel.lastSaved = std::string{ EditorText("profiler.panel.nothing_to_record", "Nothing to record: tick counters of a measured area.") };
        return;
    }
    panel.lastSaved.clear();
    profiler.StartRecording(std::move(counters));
}

void Activate(HWND owner, const ProfilerPanelGeometry& geometry, const ProfilerPanelHit& hit, int x, EditorSceneContext& context) {
    EditorProfilerState& profiler = context.Profiler();
    ProfilerPanelState& panel = profiler.Panel();
    ProfilerSelection selection = context.EditorConfig().profiler;
    const kb::core::ProfilerRegistry& registry = profiler.Registry();
    bool selectionChanged = true;
    switch (hit.action) {
    case Action::Tab: panel.tab = static_cast<ProfilerPanelTab>(hit.argument); selectionChanged = false; break;
    case Action::Record: StartOrStopRecording(context); selectionChanged = false; break;
    case Action::Pause: panel.paused = !panel.paused; selectionChanged = false; break;
    case Action::Clear: profiler.ClearMeasurements(); selectionChanged = false; break;
    case Action::FirstFrame: if (profiler.Frame() > 0U) panel.selectedFrame = OldestFrame(profiler); selectionChanged = false; break;
    case Action::PreviousFrame: case Action::NextFrame: {
        const std::optional<std::uint64_t> shown = ProfilerShownFrame(profiler);
        if (shown) {
            panel.selectedFrame = hit.action == Action::PreviousFrame ? std::max(OldestFrame(profiler), *shown - (*shown > 1U ? 1U : 0U)) : std::min(profiler.Frame(), *shown + 1U);
        }
        selectionChanged = false;
        break;
    }
    case Action::Live: panel.selectedFrame.reset(); selectionChanged = false; break;
    case Action::Snapshot: {
        const std::optional<std::uint64_t> shown = ProfilerShownFrame(profiler);
        panel.lastSaved = shown ? ProfilerFiles::SaveSnapshot(profiler, *shown) : std::string{ EditorText("profiler.panel.no_snapshot", "That frame has no regions to save.") };
        selectionChanged = false;
        break;
    }
    case Action::WhatToShow: profiler.SetWindowOpen(!profiler.WindowOpen()); selectionChanged = false; break;
    case Action::AreasAll: case Action::AreasNone:
        for (const ProfilerArea area : kAreas) selection.SetAreaMeasured(area, hit.action == Action::AreasAll);
        break;
    case Action::CountersAll: case Action::CountersNone:
        for (std::size_t counter = 0U; counter < registry.CounterCount(); ++counter) selection.SetInPanel(registry.Info(counter).id, hit.action == Action::CountersAll);
        break;
    case Action::Area: selection.SetAreaMeasured(static_cast<ProfilerArea>(hit.argument), !selection.AreaMeasured(static_cast<ProfilerArea>(hit.argument))); break;
    case Action::LeftCounter: case Action::CounterPanel: {
        const std::string_view id = registry.Info(static_cast<std::size_t>(hit.argument)).id;
        selection.SetInPanel(id, !selection.InPanel(id));
        break;
    }
    case Action::CounterScene: {
        const std::string_view id = registry.Info(static_cast<std::size_t>(hit.argument)).id;
        selection.SetOnScene(id, !selection.OnScene(id));
        break;
    }
    case Action::CounterBudget: case Action::CounterAlarm:
        EditThreshold(owner, context, static_cast<std::size_t>(hit.argument), hit.action == Action::CounterAlarm);
        selectionChanged = false;
        break;
    case Action::PickFrame: {
        const std::size_t samples = registry.Stats(ProfilerRegistry::Id(ProfilerBuiltIn::FrameTime)).samples;
        const ProfilerChartRow& chart = geometry.charts[static_cast<std::size_t>(hit.argument)];
        if (samples > 0U) panel.selectedFrame = profiler.Frame() - static_cast<std::uint64_t>(ProfilerFrameOffsetAt(chart.plot, x, samples));
        selectionChanged = false;
        break;
    }
    case Action::Severity: panel.showSeverity[static_cast<std::size_t>(hit.argument)] = !panel.showSeverity[static_cast<std::size_t>(hit.argument)]; panel.selectedFinding = -1; selectionChanged = false; break;
    case Action::Category: panel.showCategory[static_cast<std::size_t>(hit.argument)] = !panel.showCategory[static_cast<std::size_t>(hit.argument)]; panel.selectedFinding = -1; selectionChanged = false; break;
    case Action::ShowIgnored: panel.showIgnored = !panel.showIgnored; panel.selectedFinding = -1; selectionChanged = false; break;
    case Action::Finding: panel.selectedFinding = hit.argument; selectionChanged = false; break;
    case Action::ShowInMeasurements: panel.tab = ProfilerPanelTab::Measurements; selectionChanged = false; break;
    case Action::IgnoreFinding: {
        const std::vector<const kb::core::DiagnosisFinding*> findings = ProfilerVisibleFindings(profiler);
        if (panel.selectedFinding >= 0 && panel.selectedFinding < static_cast<int>(findings.size())) {
            const kb::core::DiagnosisFinding& finding = *findings[static_cast<std::size_t>(panel.selectedFinding)];
            panel.ignored.insert(finding.rule + "/" + finding.subject);
            panel.selectedFinding = -1;
        }
        selectionChanged = false;
        break;
    }
    }
    if (selectionChanged) Commit(context, selection);
}

[[nodiscard]] int Clamp(int scroll, int content, int view) { return std::clamp(scroll, 0, std::max(0, content - view)); }

} // namespace

bool ProfilerPanel::Pointer(HWND owner, const RECT& content, int x, int y, EditorSceneContext& sceneContext) {
    EditorProfilerState& profiler = sceneContext.Profiler();
    const ProfilerSelection& selection = sceneContext.EditorConfig().profiler;
    const ProfilerPanelGeometry geometry = ResolveProfilerPanelGeometry(content, profiler.Panel(), selection);
    const std::vector<ProfilerPanelHit> hits = ProfilerPanelHits(geometry, profiler, selection);
    for (std::size_t index = hits.size(); index-- > 0U;) {
        if (!Contains(hits[index].bounds, x, y)) continue;
        Activate(owner, geometry, hits[index], x, sceneContext);
        return true;
    }
    return Contains(content, x, y);
}

bool ProfilerPanel::Wheel(const RECT& content, int x, int y, int delta, EditorSceneContext& sceneContext) {
    EditorProfilerState& profiler = sceneContext.Profiler();
    ProfilerPanelState& panel = profiler.Panel();
    const ProfilerPanelGeometry geometry = ResolveProfilerPanelGeometry(content, panel, sceneContext.EditorConfig().profiler);
    const int notches = -delta / WHEEL_DELTA;
    const int rowHeight = ProfilerPanelGeometry::kRowHeight;
    const std::vector<ProfilerCounterRow> counters = ProfilerCounterRows(profiler.Registry());
    const int groups = static_cast<int>(std::ranges::count_if(counters, &ProfilerCounterRow::startsGroup));
    const auto height = [](const RECT& rect) { return static_cast<int>(rect.bottom - rect.top); };
    if (panel.tab != ProfilerPanelTab::Diagnosis && Contains(geometry.countersList, x, y)) {
        const int contentHeight = (static_cast<int>(counters.size()) + groups) * rowHeight;
        panel.leftScroll = Clamp(panel.leftScroll + notches * 3 * rowHeight, contentHeight, height(geometry.countersList));
        return true;
    }
    if (panel.tab == ProfilerPanelTab::Measurements && Contains(geometry.hierarchyRows, x, y)) {
        const std::optional<std::uint64_t> shown = ProfilerShownFrame(profiler);
        const kb::core::ProfilerFrameRecord* record = shown ? profiler.FrameRegions().Find(*shown) : nullptr;
        const int contentHeight = record == nullptr ? 0 : static_cast<int>(record->rows.size()) * rowHeight;
        panel.hierarchyScroll = Clamp(panel.hierarchyScroll + notches * 3 * rowHeight, contentHeight, height(geometry.hierarchyRows));
        return true;
    }
    if (panel.tab == ProfilerPanelTab::Counters && Contains(geometry.tableRows, x, y)) {
        const int contentHeight = static_cast<int>(counters.size()) * ProfilerPanelGeometry::kCounterRowHeight + groups * rowHeight;
        panel.countersScroll = Clamp(panel.countersScroll + notches * 3 * ProfilerPanelGeometry::kCounterRowHeight, contentHeight, height(geometry.tableRows));
        return true;
    }
    if (panel.tab == ProfilerPanelTab::Diagnosis && Contains(geometry.tableRows, x, y)) {
        const int contentHeight = static_cast<int>(ProfilerVisibleFindings(profiler).size()) * kFindingHeight;
        panel.findingsScroll = Clamp(panel.findingsScroll + notches * kFindingHeight, contentHeight, height(geometry.tableRows));
        return true;
    }
    return false;
}

} // namespace kb::editor

#endif
