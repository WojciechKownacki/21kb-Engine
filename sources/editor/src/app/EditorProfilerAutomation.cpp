#include "app/EditorProfilerAutomation.hpp"

#include "engine/core/ProfilerCounters.hpp"
#include "app/EditorHeadlessAutomation.hpp"
#include "profiler/EditorProfilerSampler.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
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
[[nodiscard]] Outcome Sample(EditorSceneContext& context, const JsonValue& step) {
    std::string error;
    const std::optional<double> frames = Number(step, "frames", error);
    const JsonValue* costs = Field(step, "frame_ms", JsonValue::Kind::Array, error, true);
    if (!frames || costs == nullptr || costs->Size() == 0U || *frames < 1.0 || *frames > 100000.0) {
        return { false, error.empty() ? "profiler_sample needs 1..100000 frames and a non-empty frame_ms" : error };
    }
    std::vector<double> milliseconds;
    for (std::size_t index = 0U; index < costs->Size(); ++index) milliseconds.push_back(costs->At(index)->AsNumber());
    const ProfilerSelection& selection = context.EditorConfig().profiler;
    for (std::size_t frame = 0U; frame < static_cast<std::size_t>(*frames); ++frame) {
        EditorProfilerSampler::Sample(context.Profiler(), selection, context.Scene(), milliseconds[frame % milliseconds.size()]);
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
    if (!id || !available || !error.empty()) return { false, error };
    const kb::core::ProfilerRegistry& registry = context.Profiler().Registry();
    const std::optional<std::size_t> counter = registry.Find(*id);
    if (!counter) return { false, "no profiler counter '" + *id + "'" };
    const kb::core::ProfilerCounterStats stats = registry.Stats(*counter);
    const std::string message = std::format("{} {}", *id, stats.available ? std::format("= {}", stats.now) : "is unavailable");
    if (stats.available != available.value()) return { false, message };
    return { !stats.available || ((!minimum || stats.now >= *minimum) && (!maximum || stats.now <= *maximum)), message };
}

} // namespace

std::optional<std::pair<bool, std::string>> EditorProfilerAutomation::Run(std::string_view operation,
    const kb::core::JsonValue& step, EditorSceneContext& context, EditorHeadlessAutomation& automation) {
    std::string error;
    if (operation == "profiler_select") return Select(context, step);
    if (operation == "profiler_sample") return Sample(context, step);
    if (operation == "assert_profiler_counter") return CheckCounter(context, step);
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
