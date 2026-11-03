#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "profiler/ProfilerOverlayLayout.hpp"
#include "profiler/ProfilerPanelLayout.hpp"
#include "settings/EditorConfigurationStore.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

using kb::core::ProfilerBuiltIn;
using kb::core::ProfilerRegistry;
using namespace kb::editor;
using kb::editor::tests::Require;

#if defined(_WIN32)

[[nodiscard]] std::filesystem::path TempRoot() {
    return std::filesystem::temp_directory_path() / "21kb_editor_profiler_panel_tests";
}

void RunThresholdsTest() {
    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
    const std::filesystem::path root = TempRoot();
    const std::filesystem::path path = EditorConfigurationStore::FilePath(root);
    std::filesystem::create_directories(path.parent_path(), error);
    EditorConfiguration written;
    written.profiler.SetThreshold("frame_ms", 8.3, 16.6);
    written.profiler.SetThreshold("System A:B", 0.0, 2.5);
    std::string saveError;
    Require(EditorConfigurationStore::Save(path, root, written, saveError), "Thresholds must save");
    const EditorConfigurationLoadResult loaded = EditorConfigurationStore::Load(path, root);
    Require(loaded.configuration.profiler.thresholds == written.profiler.thresholds && loaded.configuration.profiler.thresholds.size() == 2U,
        "Thresholds come back as set, a name with a colon included");

    ProfilerSelection selection;
    selection.SetThreshold("frame_ms", 8.3, 16.6);
    selection.SetThreshold("frame_ms", 9.0, 17.0);
    Require(selection.thresholds.size() == 1U && selection.thresholds[0].budget == 9.0, "A counter has one override, the last");
    selection.SetThreshold("frame_ms", 0.0, 0.0);
    Require(selection.thresholds.empty(), "Both zero removes the override");

    EditorProfilerState profiler;
    const std::size_t frame = ProfilerRegistry::Id(ProfilerBuiltIn::FrameTime);
    selection.SetThreshold("frame_ms", 8.3, 16.6);
    profiler.SyncThresholds(selection);
    Require(profiler.Registry().Info(frame).budget == 8.3 && profiler.Registry().Info(frame).alarm == 16.6, "The registry takes the thresholds of the user");
    selection.SetThreshold("frame_ms", 0.0, 0.0);
    profiler.SyncThresholds(selection);
    Require(profiler.Registry().Info(frame).budget == 16.6 && profiler.Registry().Info(frame).alarm == 33.3, "Without an override the thresholds of the counter are back");
    std::filesystem::remove_all(TempRoot(), error);
}

void RunPanelGeometryTest() {
    const RECT content{ 0, 0, 1000, 600 };
    ProfilerPanelState state;
    ProfilerSelection selection;  // cpu and gpu
    ProfilerPanelGeometry geometry = ResolveProfilerPanelGeometry(content, state, selection);
    Require(geometry.charts.size() == 3U && geometry.charts[0].kind == ProfilerChartKind::Frame && geometry.charts[1].kind == ProfilerChartKind::Cpu && geometry.charts[2].kind == ProfilerChartKind::Gpu,
        "CPU and GPU measured: the frame, the CPU and the GPU chart");
    selection.ApplyPreset(ProfilerPreset::Everything, ProfilerRegistry{});
    geometry = ResolveProfilerPanelGeometry(content, state, selection);
    Require(geometry.charts.size() == 5U, "Every chart row when every area is measured");
    int expectedTop = geometry.ruler.bottom;
    for (const ProfilerChartRow& chart : geometry.charts) {
        Require(chart.row.top == expectedTop && chart.row.bottom > chart.row.top && chart.plot.left == chart.label.right, "Chart rows follow one another, label then plot");
        expectedTop = chart.row.bottom;
    }
    Require(geometry.hierarchyTabs.top == expectedTop && geometry.hierarchyRows.bottom - geometry.hierarchyRows.top >= 100, "The hierarchy takes what the charts leave, and enough of it");
    Require(geometry.center.left == geometry.left.right && geometry.status.bottom == content.bottom && geometry.countersList.bottom <= geometry.status.top, "The panes fill the panel");

    state.tab = ProfilerPanelTab::Diagnosis;
    geometry = ResolveProfilerPanelGeometry(content, state, selection);
    Require(geometry.right.right - geometry.right.left == ProfilerPanelGeometry::kRightWidth && geometry.center.right == geometry.right.left && geometry.charts.empty(), "Diagnosis has its details pane and no charts");
    geometry = ResolveProfilerPanelGeometry(RECT{ 0, 0, 800, 600 }, state, selection);
    Require(geometry.right.right == geometry.right.left, "A narrow panel leaves the details out");
}

void RunPanelHitsTest() {
    EditorProfilerState profiler;
    ProfilerSelection selection;
    const RECT content{ 0, 0, 1000, 600 };
    ProfilerPanelGeometry geometry = ResolveProfilerPanelGeometry(content, profiler.Panel(), selection);
    std::vector<ProfilerPanelHit> hits = ProfilerPanelHits(geometry, profiler, selection);
    const auto count = [&](ProfilerPanelAction action) { return std::ranges::count(hits, action, &ProfilerPanelHit::action); };
    Require(count(ProfilerPanelAction::Tab) == 3 && count(ProfilerPanelAction::Area) == 5 && count(ProfilerPanelAction::PickFrame) == 3, "Tabs, areas and a chart per measured area answer clicks");
    Require(count(ProfilerPanelAction::Record) == 1 && count(ProfilerPanelAction::Pause) == 1 && count(ProfilerPanelAction::Snapshot) == 1 && count(ProfilerPanelAction::WhatToShow) == 1, "The buttons of the toolbar");
    for (const ProfilerPanelHit& hit : hits) {
        Require(hit.bounds.right > hit.bounds.left && hit.bounds.bottom > hit.bounds.top && hit.bounds.left >= content.left && hit.bounds.right <= content.right, "Every hit is a real area inside the panel");
    }
    profiler.Panel().tab = ProfilerPanelTab::Counters;
    geometry = ResolveProfilerPanelGeometry(content, profiler.Panel(), selection);
    hits = ProfilerPanelHits(geometry, profiler, selection);
    const std::ptrdiff_t visibleRows = std::ranges::count(hits, ProfilerPanelAction::CounterBudget, &ProfilerPanelHit::action);
    Require(visibleRows > 5 && visibleRows < static_cast<std::ptrdiff_t>(profiler.Registry().CounterCount()), "Only the rows of the counters table that are on screen answer, the table scrolls");
    profiler.Panel().countersScroll = 100000;
    hits = ProfilerPanelHits(geometry, profiler, selection);
    Require(std::ranges::count(hits, ProfilerPanelAction::CounterBudget, &ProfilerPanelHit::action) == 0, "A table scrolled past its end has no rows to click");

    const std::vector<ProfilerCounterRow> rows = ProfilerCounterRows(profiler.Registry());
    Require(rows.size() == profiler.Registry().CounterCount() && std::ranges::count(rows, true, &ProfilerCounterRow::startsGroup) == 4, "Frame, Drawing, Memory and ECS each start a group");
    const ProfilerCounterColumns columns = ResolveProfilerCounterColumns(RECT{ 250, 0, 1000, 400 });
    Require(columns.name < columns.now && columns.now < columns.average && columns.average < columns.minimum && columns.maximum < columns.p95 && columns.p95 < columns.budget
        && columns.budget < columns.alarm && columns.alarm < columns.spark && columns.spark < columns.scene && columns.scene < columns.end && columns.name - columns.check >= 20, "The columns run left to right without crossing");
    Require(columns.now - columns.name >= 100, "The name column has room for a name at the narrowest panel that shows the table");
}

void RunFramePickingTest() {
    const RECT plot{ 170, 0, 770, 64 };
    for (std::size_t offset = 0U; offset < ProfilerRegistry::HistoryFrames(); offset += 7U) {
        Require(ProfilerFrameOffsetAt(plot, ProfilerFrameX(plot, offset), ProfilerRegistry::HistoryFrames()) == static_cast<int>(offset), "A click on a column picks that frame");
    }
    Require(ProfilerFrameOffsetAt(plot, 5000, 100) == 0 && ProfilerFrameOffsetAt(plot, -5000, 100) == 99, "A click past an end picks the end of what there is");

    EditorProfilerState profiler;
    Require(!ProfilerShownFrame(profiler), "No frame before any is sampled");
    for (int frame = 0; frame < 300; ++frame) profiler.NextFrame();
    Require(ProfilerShownFrame(profiler) == 300U, "Following the latest frame");
    profiler.Panel().selectedFrame = 60U;
    Require(ProfilerShownFrame(profiler) == 300U, "A frame that left the history is not shown");
    profiler.Panel().selectedFrame = 150U;
    Require(ProfilerShownFrame(profiler) == 150U, "A frame still in the history is");
    profiler.Panel().selectedFrame = 400U;
    Require(ProfilerShownFrame(profiler) == 300U, "A frame that does not exist yet is not shown");
}

void RunFindingFiltersTest() {
    EditorProfilerState profiler;
    for (int index = 0; index < 240; ++index) {
        profiler.Registry().Publish(ProfilerRegistry::Id(ProfilerBuiltIn::DrawCalls), 3000.0);
        profiler.Registry().Publish(ProfilerRegistry::Id(ProfilerBuiltIn::Triangles), 6'000'000.0);
    }
    profiler.Diagnose();
    Require(ProfilerVisibleFindings(profiler).size() == 2U, "Draw calls and triangles over their thresholds");
    profiler.Panel().showSeverity[static_cast<std::size_t>(kb::core::DiagnosisSeverity::Problem)] = false;
    Require(ProfilerVisibleFindings(profiler).size() == 1U && ProfilerVisibleFindings(profiler)[0]->rule == "draw_calls_high", "The severity filter hides the problem");
    profiler.Panel().showSeverity[static_cast<std::size_t>(kb::core::DiagnosisSeverity::Problem)] = true;
    profiler.Panel().showCategory[static_cast<std::size_t>(kb::core::DiagnosisCategory::Gpu)] = false;
    Require(ProfilerVisibleFindings(profiler).size() == 1U && ProfilerVisibleFindings(profiler)[0]->rule == "draw_calls_high", "The category filter hides the GPU one");
    profiler.Panel().showCategory[static_cast<std::size_t>(kb::core::DiagnosisCategory::Gpu)] = true;
    profiler.Panel().ignored.insert("draw_calls_high/draw_calls");
    Require(ProfilerVisibleFindings(profiler).size() == 1U, "An ignored finding is not listed");
    profiler.Panel().showIgnored = true;
    Require(ProfilerVisibleFindings(profiler).size() == 2U, "Unless ignored ones are asked for");
    profiler.ClearMeasurements();
    Require(profiler.Findings().empty() && !profiler.Registry().Stats(ProfilerRegistry::Id(ProfilerBuiltIn::DrawCalls)).available, "Clearing forgets the measurements and the findings");
}

void RunOverlayAvoidTest() {
    const EditorTheme theme = MakeEditorDarkTheme();
    ProfilerRegistry registry;
    ProfilerSelection selection;
    selection.overlay.graphVisible = true;
    for (int frame = 0; frame < 240; ++frame) registry.Publish(ProfilerRegistry::Id(ProfilerBuiltIn::FrameTime), 12.0);
    const ProfilerOverlayModel model = BuildProfilerOverlayModel(registry, selection);
    ProfilerOverlayMetrics metrics = ProfilerOverlayMetrics::ForTextSize(ProfilerTextSize::Small);
    const auto graphTop = [&](const ProfilerOverlayMetrics& m) {
        ProfilerOverlayContent content;
        AppendProfilerOverlay(content, model, selection.overlay, theme, m, 1337.0F, 640.0F);
        return content.shapes.front().points[0][1];
    };
    const float free = graphTop(metrics);
    metrics.avoid = { 160.0F, 540.0F, 1170.0F, 600.0F };  // a bar across the bottom of the scene
    Require(graphTop(metrics) + metrics.graphHeight <= 540.0F - 7.0F && graphTop(metrics) < free, "A graph that would sit on the agent bar moves above it");
    metrics.avoid = { 160.0F, 540.0F, 900.0F, 600.0F };  // a bar that ends before the graph starts
    Require(graphTop(metrics) == free, "A bar that does not reach the graph leaves it where it is");
}

#endif

} // namespace

namespace kb::editor::tests {

void RunEditorProfilerPanelTests() {
#if defined(_WIN32)
    RunThresholdsTest();
    RunPanelGeometryTest();
    RunPanelHitsTest();
    RunFramePickingTest();
    RunFindingFiltersTest();
    RunOverlayAvoidTest();
#endif
}

} // namespace kb::editor::tests
