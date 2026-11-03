#include "app/EditorProfilerAutomation.hpp"

#include "engine/core/ProfilerCounters.hpp"
#include "app/EditorHeadlessAutomation.hpp"
#include "docking/EditorDockModel.hpp"
#include "profiler/EditorProfilerSampler.hpp"
#include "profiler/ProfilerPanel.hpp"
#include "profiler/ProfilerPanelLayout.hpp"
#include "project/EditorProjectPaths.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <vector>

namespace kb::editor {
namespace {

using kb::core::JsonValue;
using Outcome = std::pair<bool, std::string>;

[[nodiscard]] const JsonValue* Field(const JsonValue& step, std::string_view name, JsonValue::Kind kind, std::string& error, bool required) {
    const JsonValue* value = step.Find(name);
    if (value == nullptr) {
        if (required) error = "missing '" + std::string{ name } + "'";
        return nullptr;
    }
    if (value->GetKind() != kind) {
        error = "'" + std::string{ name } + "' has the wrong type";
        return nullptr;
    }
    return value;
}

[[nodiscard]] std::optional<std::string> Text(const JsonValue& step, std::string_view name, std::string& error, bool required = true) {
    const JsonValue* value = Field(step, name, JsonValue::Kind::String, error, required);
    return value == nullptr ? std::nullopt : std::optional<std::string>{ value->AsString() };
}

[[nodiscard]] std::optional<double> Number(const JsonValue& step, std::string_view name, std::string& error, bool required = true) {
    const JsonValue* value = Field(step, name, JsonValue::Kind::Number, error, required);
    if (value == nullptr) return std::nullopt;
    if (!std::isfinite(value->AsNumber())) {
        error = "'" + std::string{ name } + "' must be finite";
        return std::nullopt;
    }
    return value->AsNumber();
}

[[nodiscard]] std::optional<bool> Flag(const JsonValue& step, std::string_view name, std::string& error, bool required = true) {
    const JsonValue* value = Field(step, name, JsonValue::Kind::Bool, error, required);
    return value == nullptr ? std::nullopt : std::optional<bool>{ value->AsBool() };
}

// A preset (empty: none) first, then the areas (when given) and the scene's list and graph.
[[nodiscard]] Outcome Select(EditorSceneContext& context, const JsonValue& step) {
    std::string error;
    const std::string preset = Text(step, "preset", error, false).value_or("");
    const std::optional<bool> list = Flag(step, "list", error, false);
    const std::optional<bool> graph = Flag(step, "graph", error, false);
    const JsonValue* areaValues = Field(step, "areas", JsonValue::Kind::Array, error, false);
    if (!error.empty()) return { false, error };

    ProfilerSelection selection = context.EditorConfig().profiler;
    if (!preset.empty()) {
        const std::array<std::pair<std::string_view, ProfilerPreset>, 4> presets{{
            { "minimal", ProfilerPreset::Minimal }, { "rendering", ProfilerPreset::Rendering },
            { "memory", ProfilerPreset::Memory }, { "everything", ProfilerPreset::Everything } }};
        const auto found = std::ranges::find(presets, preset, &std::pair<std::string_view, ProfilerPreset>::first);
        if (found == presets.end()) return { false, "unknown profiler preset '" + preset + "'" };
        selection.ApplyPreset(found->second, context.Profiler().Registry());
    }
    if (areaValues != nullptr) {
        selection.areas.clear();
        for (std::size_t index = 0U; index < areaValues->Size(); ++index) selection.areas.emplace_back(areaValues->At(index)->AsString());
    }
    if (list) selection.overlay.listVisible = *list;
    if (graph) selection.overlay.graphVisible = *graph;
    EditorConfiguration configuration = context.EditorConfig();
    configuration.profiler = selection;
    if (!context.SaveEditorConfig(std::move(configuration))) return { false, "the editor settings could not be saved" };
    context.Profiler().RefreshOverlay(selection, std::chrono::steady_clock::now(), true);
    return { true, "profiler selection" };
}

// `frames` frames, the n-th costing the n-th of "frame_ms" in turn.
[[nodiscard]] Outcome Sample(EditorSceneContext& context, EditorHeadlessAutomation& automation, const JsonValue& step) {
    std::string error;
    const std::optional<double> frames = Number(step, "frames", error);
    const JsonValue* costs = Field(step, "frame_ms", JsonValue::Kind::Array, error, true);
    const JsonValue* games = Field(step, "game_ms", JsonValue::Kind::Array, error, false);
    if (!frames || costs == nullptr || costs->Size() == 0U || *frames < 1.0 || *frames > 100000.0) {
        return { false, error.empty() ? "profiler_sample needs 1..100000 frames and a non-empty frame_ms" : error };
    }
    std::vector<double> milliseconds;
    for (std::size_t index = 0U; index < costs->Size(); ++index) milliseconds.push_back(costs->At(index)->AsNumber());
    const ProfilerSelection& selection = context.EditorConfig().profiler;
    std::vector<double> gameMilliseconds;
    for (std::size_t index = 0U; games != nullptr && index < games->Size(); ++index) gameMilliseconds.push_back(games->At(index)->AsNumber());
    const kb::render::SceneRenderSubmitStats renderStats = automation.SceneSubmitStats();
    for (std::size_t frame = 0U; frame < static_cast<std::size_t>(*frames); ++frame) {
        EditorProfilerFrame sampled{ .presentMilliseconds = milliseconds[frame % milliseconds.size()] };
        if (!gameMilliseconds.empty()) sampled.gameTickMilliseconds = gameMilliseconds[frame % gameMilliseconds.size()];
        EditorProfilerSampler::Sample(context.Profiler(), selection, context.Scene(), sampled, &renderStats);
    }
    context.Profiler().RefreshOverlay(selection, std::chrono::steady_clock::now(), true);
    return { true, std::format("profiler frames: {}", static_cast<std::size_t>(*frames)) };
}

// Whether the counter has a reading; a reading must lie in [min, max] when they are given.
[[nodiscard]] Outcome CheckCounter(const EditorSceneContext& context, const JsonValue& step) {
    std::string error;
    const std::optional<std::string> id = Text(step, "id", error);
    const std::optional<bool> available = Flag(step, "available", error);
    const std::optional<double> minimum = Number(step, "min", error, false);
    const std::optional<double> maximum = Number(step, "max", error, false);
    const std::optional<double> samples = Number(step, "samples", error, false);
    if (!id || !available || !error.empty()) return { false, error };
    const kb::core::ProfilerRegistry& registry = context.Profiler().Registry();
    const std::optional<std::size_t> counter = registry.Find(*id);
    if (!counter) return { false, "no profiler counter '" + *id + "'" };
    const kb::core::ProfilerCounterStats stats = registry.Stats(*counter);
    const std::string message = std::format("{} {}", *id, stats.available ? std::format("= {}", stats.now) : "is unavailable");
    if (stats.available != available.value()) return { false, message };
    if (samples && static_cast<double>(stats.samples) != *samples) return { false, message + std::format(" with {} samples", stats.samples) };
    return { !stats.available || ((!minimum || stats.now >= *minimum) && (!maximum || stats.now <= *maximum)), message };
}

[[nodiscard]] std::optional<ProfilerPanelAction> ActionNamed(std::string_view name) {
    static constexpr std::array<std::pair<std::string_view, ProfilerPanelAction>, 25> names{ {
        { "tab", ProfilerPanelAction::Tab }, { "record", ProfilerPanelAction::Record }, { "pause", ProfilerPanelAction::Pause },
        { "clear", ProfilerPanelAction::Clear }, { "first_frame", ProfilerPanelAction::FirstFrame }, { "previous_frame", ProfilerPanelAction::PreviousFrame },
        { "next_frame", ProfilerPanelAction::NextFrame }, { "live", ProfilerPanelAction::Live }, { "snapshot", ProfilerPanelAction::Snapshot },
        { "what_to_show", ProfilerPanelAction::WhatToShow }, { "areas_all", ProfilerPanelAction::AreasAll }, { "areas_none", ProfilerPanelAction::AreasNone },
        { "counters_all", ProfilerPanelAction::CountersAll }, { "counters_none", ProfilerPanelAction::CountersNone }, { "area", ProfilerPanelAction::Area },
        { "left_counter", ProfilerPanelAction::LeftCounter }, { "pick_frame", ProfilerPanelAction::PickFrame }, { "counter_panel", ProfilerPanelAction::CounterPanel },
        { "counter_scene", ProfilerPanelAction::CounterScene }, { "severity", ProfilerPanelAction::Severity }, { "category", ProfilerPanelAction::Category },
        { "show_ignored", ProfilerPanelAction::ShowIgnored }, { "finding", ProfilerPanelAction::Finding },
        { "show_in_measurements", ProfilerPanelAction::ShowInMeasurements }, { "ignore_finding", ProfilerPanelAction::IgnoreFinding } } };
    const auto found = std::ranges::find(names, name, &std::pair<std::string_view, ProfilerPanelAction>::first);
    return found == names.end() ? std::nullopt : std::optional<ProfilerPanelAction>{ found->second };
}

// A click on the part of the panel that has `action` (and `argument`), at its center; "pick_offset" places
// the click of a chart on the column that many frames before the newest. A threshold cell is left to the
// person, since it opens a dialog.
[[nodiscard]] Outcome ClickPanel(EditorSceneContext& context, const JsonValue& step) {
    std::string error;
    const std::optional<std::string> name = Text(step, "action", error);
    const int argument = static_cast<int>(Number(step, "argument", error, false).value_or(0.0));
    const std::optional<double> offset = Number(step, "pick_offset", error, false);
    const int width = static_cast<int>(Number(step, "width", error, false).value_or(1000.0));
    const int height = static_cast<int>(Number(step, "height", error, false).value_or(600.0));
    if (!name || !error.empty()) return { false, error };
    const std::optional<ProfilerPanelAction> action = ActionNamed(*name);
    if (!action) return { false, "unknown profiler panel action '" + *name + "'" };
    const RECT content{ 0, 0, width, height };
    const ProfilerSelection& selection = context.EditorConfig().profiler;
    const ProfilerPanelGeometry geometry = ResolveProfilerPanelGeometry(content, context.Profiler().Panel(), selection);
    for (const ProfilerPanelHit& hit : ProfilerPanelHits(geometry, context.Profiler(), selection)) {
        if (hit.action != *action || hit.argument != argument) continue;
        const int y = (hit.bounds.top + hit.bounds.bottom) / 2;
        const int x = offset ? ProfilerFrameX(hit.bounds, static_cast<std::size_t>(*offset)) : (hit.bounds.left + hit.bounds.right) / 2;
        return { ProfilerPanel::Pointer(nullptr, content, x, y, context), "profiler panel " + *name };
    }
    return { false, "the profiler panel has nothing to click for '" + *name + "' " + std::to_string(argument) };
}

[[nodiscard]] Outcome WheelPanel(EditorSceneContext& context, const JsonValue& step) {
    std::string error;
    const std::optional<double> x = Number(step, "x", error);
    const std::optional<double> y = Number(step, "y", error);
    const std::optional<double> notches = Number(step, "notches", error);
    const int width = static_cast<int>(Number(step, "width", error, false).value_or(1000.0));
    const int height = static_cast<int>(Number(step, "height", error, false).value_or(600.0));
    if (!x || !y || !notches || !error.empty()) return { false, error };
    return { ProfilerPanel::Wheel(RECT{ 0, 0, width, height }, static_cast<int>(*x), static_cast<int>(*y), static_cast<int>(-*notches * WHEEL_DELTA), context), "profiler panel wheel" };
}

// The newest file of Saved/Profiler that starts with `kind`: its first line holds `header` and it has
// at least `minLines` lines in all.
[[nodiscard]] Outcome CheckFile(const JsonValue& step) {
    std::string error;
    const std::optional<std::string> kind = Text(step, "kind", error);
    const std::optional<std::string> header = Text(step, "header", error);
    const int minimum = static_cast<int>(Number(step, "min_lines", error, false).value_or(2.0));
    if (!kind || !header || !error.empty()) return { false, error };
    const std::filesystem::path folder = EditorProjectPaths::ProjectRoot() / "Saved" / "Profiler";
    std::filesystem::path newest;
    std::error_code fileError;
    for (const auto& entry : std::filesystem::directory_iterator(folder, fileError)) {
        const std::string name = entry.path().filename().string();
        if (name.starts_with(*kind) && (newest.empty() || entry.last_write_time() > std::filesystem::last_write_time(newest))) newest = entry.path();
    }
    if (newest.empty()) return { false, "no " + *kind + " file in Saved/Profiler" };
    std::ifstream input{ newest };
    std::string line;
    std::getline(input, line);
    int lines = 1;
    for (std::string rest; std::getline(input, rest);) ++lines;
    if (line != *header) return { false, newest.filename().string() + " starts with: " + line };
    return { lines >= minimum, std::format("{} has {} lines", newest.filename().string(), lines) };
}

// Whether the diagnosis of the measurements holds (or lacks) a finding of a rule, at a severity.
[[nodiscard]] Outcome CheckFinding(EditorSceneContext& context, const JsonValue& step) {
    std::string error;
    const std::optional<std::string> rule = Text(step, "rule", error);
    const std::optional<bool> present = Flag(step, "present", error);
    const std::optional<std::string> severity = Text(step, "severity", error, false);
    if (!rule || !present || !error.empty()) return { false, error };
    context.Profiler().Diagnose();
    const auto& findings = context.Profiler().Findings();
    const auto found = std::ranges::find(findings, *rule, &kb::core::DiagnosisFinding::rule);
    std::string detail = "finding " + *rule + (found == findings.end() ? " absent" : " present");
    if (found == findings.end() || !severity) return { !present.value(), detail };
    const std::array<std::string_view, 3> names{ "problem", "warning", "tip" };
    return { present.value() && names[static_cast<std::size_t>(found->severity)] == *severity, detail + " as " + std::string{ names[static_cast<std::size_t>(found->severity)] } };
}

// Opens the panel as the Options menu does, then checks where it went: shown, in a group of its own
// (the one of the Console), with the Scene View still the shown panel of its own group.
[[nodiscard]] Outcome OpenPanel(EditorDockModel& dock) {
    if (!dock.Commands().ActivatePanelKind(DockPanelKind::Profiler, ProfilerPanel::kOpenArea)) return { false, "the workspace has no Profiler panel" };
    const EditorMetrics metrics{};
    const DockLayout layout = dock.Queries().BuildLayout(1600, 960, metrics.menuHeight, metrics.statusBarHeight,
        metrics.tabStripHeight, metrics.tabMinWidth, metrics.tabWidth, metrics.splitterSize);
    const auto entryOf = [&](DockPanelKind kind) -> const DockPanelLayout* {
        for (const DockPanel& panel : dock.Queries().Panels()) {
            if (panel.kind != kind) continue;
            const auto found = std::ranges::find(layout.panels, panel.id, &DockPanelLayout::panelId);
            return found == layout.panels.end() ? nullptr : &*found;
        }
        return nullptr;
    };
    const DockPanelLayout* profiler = entryOf(DockPanelKind::Profiler);
    const DockPanelLayout* scene = entryOf(DockPanelKind::Scene);
    const DockPanelLayout* console = entryOf(DockPanelKind::Console);
    if (profiler == nullptr || scene == nullptr || console == nullptr) return { false, "the Profiler, the Scene View or the Console has no place in the layout" };
    if (!profiler->active || profiler->content.Empty()) return { false, "the Profiler is not the shown panel of its group" };
    if (profiler->leafId == scene->leafId) return { false, "the Profiler opened in the group of the Scene View" };
    if (!scene->active || scene->content.Empty()) return { false, "the Scene View is no longer shown" };
    if (profiler->leafId != console->leafId) return { false, "the Profiler is not in the group of the Console" };
    return { true, "profiler panel opened beside the Console" };
}

} // namespace

std::optional<std::pair<bool, std::string>> EditorProfilerAutomation::Run(std::string_view operation,
    const kb::core::JsonValue& step, EditorSceneContext& context, EditorHeadlessAutomation& automation, EditorDockModel& dock) {
    std::string error;
    if (operation == "profiler_open_panel") return OpenPanel(dock);
    if (operation == "profiler_select") return Select(context, step);
    if (operation == "profiler_sample") return Sample(context, automation, step);
    if (operation == "assert_profiler_counter") return CheckCounter(context, step);
    if (operation == "profiler_panel_click") return ClickPanel(context, step);
    if (operation == "profiler_panel_wheel") return WheelPanel(context, step);
    if (operation == "assert_profiler_file") return CheckFile(step);
    if (operation == "assert_profiler_finding") return CheckFinding(context, step);
    if (operation == "assert_profiler") {
        // The debug-only scopes, counters, timeline and allocations of kb::core::ProfilerCounters.
        kb::core::ProfilerCounters profiler;
        { auto scope = profiler.BeginScope("tick"); profiler.Counter("entities", 42U); profiler.Allocation(); }
        const bool valid = kb::core::ProfilerCounters::Enabled() && profiler.scopes == 1U &&
            profiler.timelineEvents == 1U && profiler.allocations == 1U &&
            profiler.Counters().size() == 1U && profiler.Counters().front().value == 42U &&
            profiler.TimelineEvents().size() == 1U;
        return Outcome{ valid, valid ? "debug-only profiler scopes, counters, timeline and allocations"
                                    : "profiler contract rejected" };
    }
    if (operation == "capture_profiler_window" || operation == "profiler_window_click") {
        // The "What to show" window drawn at width x height: captured, or clicked at (x, y).
        const double width = Number(step, "width", error, false).value_or(1180.0);
        const double height = Number(step, "height", error, false).value_or(740.0);
        if (operation == "capture_profiler_window") {
            const std::optional<std::string> checkpoint = Text(step, "checkpoint", error);
            if (!checkpoint || !error.empty()) return Outcome{ false, error };
            return Outcome{ automation.CaptureProfilerWindow(*checkpoint, static_cast<int>(width), static_cast<int>(height)), "profiler window:" + *checkpoint };
        }
        const std::optional<double> x = Number(step, "x", error);
        const std::optional<double> y = Number(step, "y", error);
        if (!x || !y || !error.empty()) return Outcome{ false, error };
        return Outcome{ automation.ClickProfilerWindow(static_cast<int>(*x), static_cast<int>(*y), static_cast<int>(width), static_cast<int>(height)),
                        "profiler window click" };
    }
    if (operation == "capture_profiler_overlay") {
        // The scene's counters and graph laid over the scene viewport capture named "scene".
        const std::optional<std::string> checkpoint = Text(step, "checkpoint", error);
        const std::optional<std::string> scene = Text(step, "scene", error);
        if (!checkpoint || !scene) return Outcome{ false, error };
        return Outcome{ automation.CaptureProfilerOverlay(*checkpoint, *scene), "profiler overlay:" + *checkpoint };
    }
    return std::nullopt;
}

} // namespace kb::editor
