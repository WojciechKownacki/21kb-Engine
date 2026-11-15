#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "profiler/EditorProfilerRegions.hpp"
#include "profiler/ProfilerOverlayLayout.hpp"
#include "localization/EditorLocalization.hpp"
#include "profiler/ProfilerFiles.hpp"
#include "profiler/ProfilerHealth.hpp"
#include "profiler/ProfilerNumber.hpp"
#include "profiler/ProfilerText.hpp"
#include "profiler/ProfilerPanelLayout.hpp"
#include "project/EditorProjectPaths.hpp"
#include "settings/EditorConfigurationStore.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

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
    Require(geometry.left.right == geometry.left.left && geometry.center.left == content.left && geometry.center.right == content.right,
        "Measurements has no left pane: the budget table takes the whole width");
    Require(geometry.frameStrip.top == geometry.health.bottom && geometry.frameStrip.bottom > geometry.frameStrip.top &&
            geometry.framePlot.left > geometry.frameStrip.left && geometry.framePlot.right < geometry.frameStrip.right &&
            geometry.budgetHeader.top == geometry.frameStrip.bottom,
        "The strip of frames, then the header of the budget table");
    Require(geometry.budgetRows.size() == 6U && geometry.budgetRows.front().counter == ProfilerBuiltIn::GameThreadTime, "A row for each counter that speaks for an area");
    int expectedTop = geometry.budgetHeader.bottom;
    std::size_t switches = 0U;
    for (const ProfilerBudgetRow& row : geometry.budgetRows) {
        Require(row.bounds.top == expectedTop && row.bounds.bottom - row.bounds.top == ProfilerPanelGeometry::kBudgetRowHeight, "Budget rows follow one another at their full height");
        const RECT toggle = ProfilerBudgetSwitchRect(row.bounds);
        Require(toggle.top >= row.bounds.top && toggle.bottom <= row.bounds.bottom && toggle.right <= row.bounds.right, "The switch of a row is inside it");
        switches += row.startsArea ? 1U : 0U;
        expectedTop = row.bounds.bottom;
    }
    Require(switches == 5U && !geometry.budgetRows[1].startsArea, "Each area has one switch, on its first row");
    Require(geometry.hierarchyTabs.top == expectedTop && geometry.hierarchyRows.bottom - geometry.hierarchyRows.top >= 100, "The hierarchy takes what the table leaves, and enough of it");
    Require(geometry.status.bottom == content.bottom && geometry.hierarchyRows.bottom == geometry.status.top, "The panes fill the panel");
    Require(geometry.health.top == geometry.toolbar.bottom && geometry.health.bottom - geometry.health.top == ProfilerPanelGeometry::kHealthHeight && !geometry.healthCompact,
        "The overview sits between the toolbar and everything else");
    const ProfilerBudgetColumns budgetColumns = ResolveProfilerBudgetColumns(geometry.budgetRows.front().bounds);
    Require(budgetColumns.name < budgetColumns.spark && budgetColumns.spark < budgetColumns.now && budgetColumns.now < budgetColumns.average &&
            budgetColumns.average < budgetColumns.budget && budgetColumns.budget < budgetColumns.toggle && budgetColumns.toggle < budgetColumns.end,
        "The columns of the budget table run left to right without crossing");

    // A panel docked in a low strip: the overview on one line, every row of the table, and neither the strip nor the hierarchy squeezed in.
    geometry = ResolveProfilerPanelGeometry(RECT{ 0, 0, 1920, 300 }, state, selection);
    Require(geometry.healthCompact && geometry.health.bottom - geometry.health.top == ProfilerPanelGeometry::kHealthCompactHeight, "A low panel has the overview on one line");
    Require(geometry.budgetRows.size() == 6U && geometry.budgetRows.back().bounds.bottom <= geometry.status.top, "A low panel keeps every budget row above the status line");
    for (const ProfilerBudgetRow& row : geometry.budgetRows) {
        Require(row.bounds.bottom - row.bounds.top >= 22, "A row of a low panel is still tall enough to read");
    }
    Require(geometry.frameStrip.bottom == geometry.frameStrip.top, "A low panel leaves the strip of frames out");
    Require(geometry.hierarchyRows.bottom == geometry.hierarchyRows.top && geometry.hierarchyTabs.bottom == geometry.hierarchyTabs.top, "A low panel leaves the hierarchy out");
    geometry = ResolveProfilerPanelGeometry(RECT{ 0, 0, 960, 200 }, state, selection);
    Require(geometry.budgetRows.size() == 2U && geometry.budgetRows.back().bounds.bottom <= geometry.status.top,
        "A panel with room for two readable rows shows the first two rather than none");
    geometry = ResolveProfilerPanelGeometry(RECT{ 0, 0, 900, 160 }, state, selection);
    Require(geometry.health.bottom == geometry.health.top, "A panel lower still has no overview");
    for (const ProfilerBudgetRow& row : geometry.budgetRows) {
        Require(row.bounds.bottom <= geometry.status.top, "No row of the lowest panel runs under the status line");
    }

    state.tab = ProfilerPanelTab::Diagnosis;
    geometry = ResolveProfilerPanelGeometry(content, state, selection);
    Require(geometry.right.right - geometry.right.left == ProfilerPanelGeometry::kRightWidth && geometry.center.right == geometry.right.left && geometry.budgetRows.empty(), "Diagnosis has its details pane and no budget table");
    geometry = ResolveProfilerPanelGeometry(RECT{ 0, 0, 800, 600 }, state, selection);
    Require(geometry.right.right == geometry.right.left, "A narrow panel leaves the details out");
}

// One measured frame of the given length: six tenths of it game code, three tenths the render thread.
void PublishHealthFrame(ProfilerRegistry& registry, double frameMilliseconds) {
    registry.Publish(ProfilerRegistry::Id(ProfilerBuiltIn::FrameTime), frameMilliseconds);
    registry.Publish(ProfilerRegistry::Id(ProfilerBuiltIn::FramesPerSecond), 1000.0 / frameMilliseconds);
    registry.Publish(ProfilerRegistry::Id(ProfilerBuiltIn::GameThreadTime), frameMilliseconds * 0.6);
    registry.Publish(ProfilerRegistry::Id(ProfilerBuiltIn::RenderThreadTime), frameMilliseconds * 0.3);
}

void RunHealthTest() {
    ProfilerSelection selection;  // cpu and gpu
    ProfilerRegistry registry;
    ProfilerHealth health = BuildProfilerHealth(registry, selection);
    Require(health.level == ProfilerHealthLevel::Waiting && !health.headline.empty() && !health.advice.empty(), "Before any frame the overview says it is waiting");
    Require(health.cards.size() == 4U && !health.cards[0].available && health.cards[0].value == "-", "CPU and GPU measured: FPS, frame, game thread and GPU cards, empty until measured");

    for (int frame = 0; frame < 60; ++frame) PublishHealthFrame(registry, 8.0);
    health = BuildProfilerHealth(registry, selection);
    Require(health.level == ProfilerHealthLevel::Smooth && health.headline.find("125") != std::string::npos, "Frames inside the budget run smoothly, and the headline names the FPS");
    Require(health.cards[0].available && health.cards[0].value.find("125") != std::string::npos && health.cards[1].value.find("8") != std::string::npos,
        "The cards carry the readings of their counters");

    PublishHealthFrame(registry, 40.0);
    PublishHealthFrame(registry, 40.0);
    PublishHealthFrame(registry, 40.0);
    PublishHealthFrame(registry, 40.0);
    health = BuildProfilerHealth(registry, selection);
    Require(health.level == ProfilerHealthLevel::Hitches, "A few slow frames among fast ones are hitches, not a slow game");

    ProfilerRegistry slow;
    for (int frame = 0; frame < 60; ++frame) PublishHealthFrame(slow, 20.0);
    health = BuildProfilerHealth(slow, selection);
    Require(health.level == ProfilerHealthLevel::BelowTarget && health.advice.find("16.6") != std::string::npos, "Frames over the budget are below the target, and the reason names the target");
    Require(health.advice.find("12.0") != std::string::npos, "The reason names the part of the frame that takes the longest");
    Require(health.cards[1].severity == kb::core::ProfilerSeverity::Warning, "A reading over its budget is marked on its card");

    ProfilerRegistry slower;
    for (int frame = 0; frame < 60; ++frame) PublishHealthFrame(slower, 50.0);
    Require(BuildProfilerHealth(slower, selection).level == ProfilerHealthLevel::TooSlow, "Frames over the alarm are too slow");

    selection.ApplyPreset(ProfilerPreset::Everything, registry);
    Require(BuildProfilerHealth(registry, selection).cards.size() == 6U, "Every area measured: draw calls and memory join the cards");
    selection.SetAreaMeasured(kb::core::ProfilerArea::Cpu, false);
    health = BuildProfilerHealth(registry, selection);
    Require(health.level == ProfilerHealthLevel::Waiting && health.cards.size() == 3U, "Without the CPU area there is no verdict and no frame cards");
}

void RunPanelHitsTest() {
    EditorProfilerState profiler;
    ProfilerSelection selection;
    const RECT content{ 0, 0, 1000, 600 };
    ProfilerPanelGeometry geometry = ResolveProfilerPanelGeometry(content, profiler.Panel(), selection);
    std::vector<ProfilerPanelHit> hits = ProfilerPanelHits(geometry, profiler, selection);
    const auto count = [&](ProfilerPanelAction action) { return std::ranges::count(hits, action, &ProfilerPanelHit::action); };
    Require(count(ProfilerPanelAction::Tab) == 3 && count(ProfilerPanelAction::Area) == 5 && count(ProfilerPanelAction::PickFrame) == 1, "Tabs, the switches of the areas and the strip of frames answer clicks");
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
    const auto sampleFrame = [&] {
        profiler.NextFrame();
        profiler.Registry().Publish(ProfilerRegistry::Id(ProfilerBuiltIn::FrameTime), 10.0);
    };
    for (int frame = 0; frame < 300; ++frame) sampleFrame();
    Require(ProfilerShownFrame(profiler) == 300U, "Following the latest frame");
    profiler.Panel().selectedFrame = 60U;
    Require(ProfilerShownFrame(profiler) == 300U, "A frame that left the history is not shown");
    profiler.Panel().selectedFrame = 150U;
    Require(ProfilerShownFrame(profiler) == 150U, "A frame still in the history is");
    profiler.Panel().selectedFrame = 400U;
    Require(ProfilerShownFrame(profiler) == 300U, "A frame that does not exist yet is not shown");

    // After the measurements are cleared the charts hold only the frames sampled since, so the frames
    // before are gone from them (their number is no column of a chart) and the oldest is not 61.
    profiler.ClearMeasurements();
    for (int frame = 0; frame < 50; ++frame) sampleFrame();
    Require(ProfilerOldestFrame(profiler) == 301U && ProfilerShownFrame(profiler) == 350U, "Fifty frames since the clear: 301 to 350");
    profiler.Panel().selectedFrame = 300U;
    Require(ProfilerShownFrame(profiler) == 350U, "A frame from before the clear is not shown");
    profiler.Panel().selectedFrame = 320U;
    Require(ProfilerShownFrame(profiler) == 320U && ProfilerFrameOffsetAt(RECT{ 170, 0, 770, 64 }, ProfilerFrameX(RECT{ 170, 0, 770, 64 }, 30U), 50U) == 30, "A frame since the clear is, 30 columns from the newest");
}

void RunFindingFiltersTest() {
    EditorProfilerState profiler;
    for (int index = 0; index < 240; ++index) {
        profiler.Registry().Publish(ProfilerRegistry::Id(ProfilerBuiltIn::DrawCalls), 3000.0);
        profiler.Registry().Publish(ProfilerRegistry::Id(ProfilerBuiltIn::Triangles), 6'000'000.0);
    }
    profiler.Diagnose();
    Require(ProfilerVisibleFindings(profiler).size() == 2U, "Draw calls and triangles over their thresholds");
    // The picked finding is the one whose details show, wherever the next run puts it in the list.
    profiler.Panel().selectedFinding = ProfilerVisibleFindings(profiler)[1]->Key();
    Require(ProfilerSelectedFinding(profiler) != nullptr && ProfilerSelectedFinding(profiler)->rule == "draw_calls_high", "The triangles are a problem and come first, so the draw calls are the second");
    for (int index = 0; index < 240; ++index) profiler.Registry().Publish(ProfilerRegistry::Id(ProfilerBuiltIn::Triangles), 100.0);
    profiler.Diagnose();
    Require(ProfilerVisibleFindings(profiler).size() == 1U && ProfilerSelectedFinding(profiler) != nullptr && ProfilerSelectedFinding(profiler)->rule == "draw_calls_high",
        "The draw calls moved to the first place and are still the picked finding");
    for (int index = 0; index < 240; ++index) profiler.Registry().Publish(ProfilerRegistry::Id(ProfilerBuiltIn::DrawCalls), 100.0);
    profiler.Diagnose();
    Require(ProfilerSelectedFinding(profiler) == nullptr, "A picked finding that is gone shows no details");
    for (int index = 0; index < 240; ++index) {
        profiler.Registry().Publish(ProfilerRegistry::Id(ProfilerBuiltIn::DrawCalls), 3000.0);
        profiler.Registry().Publish(ProfilerRegistry::Id(ProfilerBuiltIn::Triangles), 6'000'000.0);
    }
    profiler.Diagnose();
    Require(ProfilerSelectedFinding(profiler) != nullptr && ProfilerSelectedFinding(profiler)->rule == "draw_calls_high", "A finding that comes back is the picked one again");
    profiler.Panel().selectedFinding.clear();
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

void RunNumberFormatTest() {
    // What a person reads follows the language: a comma in Polish, a point in English. The CSV is for
    // machines and keeps the point in both.
    kb::core::ProfilerRegistry registry;
    const std::size_t frame = ProfilerRegistry::Id(ProfilerBuiltIn::FrameTime);
    registry.Publish(frame, 12.34);
    kb::core::ProfilerRecorder recorder;
    recorder.Start(registry, { frame });
    registry.Publish(frame, 12.34);
    recorder.Capture(registry, 1U, 0.0);
    const kb::core::DiagnosisFinding finding{ .rule = "hot_region", .subject = "Update", .value = 50.0, .limit = 40.0, .gainMilliseconds = 3.5 };
    std::string error;
    Require(EditorLocalization::SetLanguage("en", {}, error) && FormatProfilerValue(registry.Info(frame), 12.34) == "12.3 ms", "English writes a point");
    Require(ProfilerText::FindingDetail(finding).find("3.5") != std::string::npos, "English diagnosis writes a point");
    Require(EditorLocalization::SetLanguage("pl", KB_EDITOR_LOCALIZATION_DIR, error), "The Polish catalog loads");
    Require(FormatProfilerValue(registry.Info(frame), 12.34) == "12,3 ms" && ProfilerNumber::Fixed(0.5, 2) == "0,50" && ProfilerNumber::General(16.6) == "16,6",
        "Polish writes a comma in what the overlay and the panel show");
    Require(ProfilerText::FindingDetail(finding).find("3,5") != std::string::npos, "Polish diagnosis writes a comma");
    Require(recorder.ToCsv(registry).find("12.3400") != std::string::npos, "The CSV keeps the point in Polish");
    Require(ProfilerNumber::Localized("1.5", "de") == "1,5" && ProfilerNumber::Localized("1.5", "ja") == "1.5" && ProfilerNumber::Localized("1.5", "pt-BR") == "1,5",
        "The languages that write a comma are the ones of CLDR");
    Require(EditorLocalization::SetLanguage("en", {}, error), "Back to English");
}

void RunOverlayMemoryTest() {
    // A scene overlay that does not change is not drawn again: the memory knows what the window shows.
    const std::vector<EditorSceneViewportTextLabel> labels{ EditorSceneViewportTextLabel{ .x = 10.0F, .y = 20.0F, .text = "FPS 60" } };
    std::vector<EditorSceneViewportOverlayShape> shapes{ EditorSceneViewportOverlayShape{ .points = { { 0.0F, 0.0F }, { 4.0F, 4.0F } } } };
    EditorSceneViewportOverlayMemory memory;
    Require(!memory.Shows(labels, shapes, 5, 6), "A window with nothing remembered must be drawn");
    memory.Remember(labels, shapes, 5, 6);
    Require(memory.Shows(labels, shapes, 5, 6), "The same content at the same place is not drawn again");
    Require(!memory.Shows(labels, shapes, 5, 7), "A window that moved is drawn again");
    std::vector<EditorSceneViewportTextLabel> changed = labels;
    changed[0].text = "FPS 61";
    Require(!memory.Shows(changed, shapes, 5, 6), "A changed label is drawn again");
    shapes[0].points[1][0] = 5.0F;
    Require(!memory.Shows(labels, shapes, 5, 6), "A changed shape is drawn again");
    memory.Forget();
    Require(!memory.Shows(labels, std::vector<EditorSceneViewportOverlayShape>{ shapes }, 5, 6), "A forgotten picture is drawn again");
}

void RunCounterSearchTest() {
    kb::core::ProfilerRegistry registry;
    const kb::core::ProfilerCounterInfo& draws = registry.Info(ProfilerRegistry::Id(ProfilerBuiltIn::DrawCalls));
    const kb::core::ProfilerCounterInfo& gpu = registry.Info(ProfilerRegistry::Id(ProfilerBuiltIn::GpuTime));
    std::string error;
    Require(EditorLocalization::SetLanguage("en", {}, error), "English");
    Require(ProfilerText::CounterMatches(draws, "") && ProfilerText::CounterMatches(draws, "DRAW") && ProfilerText::CounterMatches(draws, "calls") && !ProfilerText::CounterMatches(gpu, "draw"),
        "A search is a part of the name, whatever the case; an empty one lists everything");
    Require(ProfilerText::CounterMatches(draws, "draw_c") && ProfilerText::CounterMatches(gpu, "gpu_ms"), "The id answers a search too");
    Require(EditorLocalization::SetLanguage("pl", KB_EDITOR_LOCALIZATION_DIR, error) && ProfilerText::CounterMatches(draws, "rysow") && !ProfilerText::CounterMatches(draws, "draw calls"),
        "The name searched is the one in the editor's language");
    Require(EditorLocalization::SetLanguage("en", {}, error), "Back to English");
}

void RunGpuFramesTest() {
    EditorProfilerState profiler;
    Require(profiler.NewGpuFrame(7U), "The first GPU result is new");
    Require(!profiler.NewGpuFrame(7U) && !profiler.NewGpuFrame(7U), "bgfx repeats a result until the next is ready, and a repeat is no reading");
    Require(profiler.NewGpuFrame(8U) && profiler.NewGpuFrame(7U), "A result of another frame is a reading");
}

void RunSavedFilesTest() {
    const std::filesystem::path folder = EditorProjectPaths::ProjectRoot() / "Saved" / "Profiler";
    const auto recordings = [&] {
        std::set<std::filesystem::path> found;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
            if (entry.path().filename().string().starts_with("recording-")) found.insert(entry.path());
        }
        return found;
    };
    const std::set<std::filesystem::path> before = recordings();
    EditorProfilerState profiler;
    const std::string first = ProfilerFiles::SaveRecording(profiler);
    const std::string second = ProfilerFiles::SaveRecording(profiler);
    std::set<std::filesystem::path> created = recordings();
    std::error_code error;
    for (const std::filesystem::path& path : before) created.erase(path);
    for (const std::filesystem::path& path : created) std::filesystem::remove(path, error);
    Require(created.size() == 2U && first != second, "Two recordings saved in the same second are two files, the second does not overwrite the first");
}

void RunFrameRegionsTest() {
    // A 17 ms frame: 8 ms of game tick whose update took 6 (1 of them syncing transforms) and ran two
    // systems that took 4 and 8 ms of CPU on several threads, then a 10 ms present that waited 5.
    constexpr std::uint64_t kMs = 1'000'000U;
    const std::array<kb::ecs::SystemSchedulerSystemCounters, 2> systems{ kb::ecs::SystemSchedulerSystemCounters{ .systemName = "A", .cpuTimeNanoseconds = 4 * kMs },
        kb::ecs::SystemSchedulerSystemCounters{ .systemName = "B", .cpuTimeNanoseconds = 8 * kMs } };
    kb::core::ProfilerCounters regions{ true };
    RecordProfilerRegions(regions, EditorProfilerFrame{ .presentMilliseconds = 10.0, .beginMilliseconds = 1.0, .mainMilliseconds = 2.0,
        .endMilliseconds = 6.0, .waitMilliseconds = 5.0, .gameTickMilliseconds = 8.0 },
        EditorProfilerSceneTimes{ .updateNanoseconds = 6 * kMs, .transformSyncNanoseconds = 1 * kMs, .systems = systems });
    const std::vector<kb::core::ProfilerRegionRow> rows = kb::core::ProfilerFrameRegions::Aggregate(regions.TimelineEvents());
    const auto row = [&](std::string_view name) -> const kb::core::ProfilerRegionRow* {
        const auto found = std::ranges::find(rows, name, &kb::core::ProfilerRegionRow::name);
        return found == rows.end() ? nullptr : &*found;
    };
    const auto within = [](std::uint64_t value, std::uint64_t expected) { return value + 10U >= expected && value <= expected + 10U; };
    const kb::core::ProfilerRegionRow* update = row("Scene update");
    const kb::core::ProfilerRegionRow* sync = row("Transform sync");
    const kb::core::ProfilerRegionRow* a = row("A");
    const kb::core::ProfilerRegionRow* b = row("B");
    Require(update && sync && a && b && row("Game tick") && row("Present"), "The frame has its regions");
    Require(sync->depth == update->depth + 1U && a->depth == update->depth + 1U && b->depth == update->depth + 1U, "The systems and the transform sync are inside the scene update");
    Require(within(a->totalNanoseconds + b->totalNanoseconds, 5 * kMs) && b->totalNanoseconds > a->totalNanoseconds, "Systems that took 12 ms of CPU in a 5 ms update show their shares of the 5 ms");
    Require(within(update->selfNanoseconds, 0U) && within(row("Game tick")->selfNanoseconds, 2 * kMs), "The update is its systems and its sync, and the tick has the 2 ms outside it");
    Require(row("Wait for the previous frame") != nullptr && row("Wait for the previous frame")->waiting && within(row("Wait for the previous frame")->totalNanoseconds, 5 * kMs), "The wait is a waiting region of 5 ms");
    Require(within(row("End frame")->selfNanoseconds, 1 * kMs) && within(row("Present")->selfNanoseconds, 1 * kMs), "The end of the frame is 1 ms of work and a wait, the present has 1 ms outside its stages");
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
    RunHealthTest();
    RunPanelHitsTest();
    RunFramePickingTest();
    RunFindingFiltersTest();
    RunNumberFormatTest();
    RunOverlayMemoryTest();
    RunCounterSearchTest();
    RunGpuFramesTest();
    RunSavedFilesTest();
    RunFrameRegionsTest();
    RunOverlayAvoidTest();
#endif
}

} // namespace kb::editor::tests
