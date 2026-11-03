#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "profiler/ProfilerOverlayLayout.hpp"
#include "profiler/ProfilerPalette.hpp"
#include "settings/EditorConfigurationStore.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace {

using kb::core::ProfilerArea;
using kb::core::ProfilerBuiltIn;
using kb::core::ProfilerRegistry;
using kb::core::ProfilerSeverity;
using namespace kb::editor;
using kb::editor::tests::Require;

[[nodiscard]] std::filesystem::path TempRoot() {
    return std::filesystem::temp_directory_path() / "21kb_editor_profiler_tests";
}

void Publish(ProfilerRegistry& registry, ProfilerBuiltIn counter, double value) {
    registry.Publish(ProfilerRegistry::Id(counter), value);
}

[[nodiscard]] const ProfilerOverlayLine* FindLine(const ProfilerOverlayModel& model, std::string_view label) {
    const auto found = std::ranges::find(model.lines, label, &ProfilerOverlayLine::label);
    return found == model.lines.end() ? nullptr : &*found;
}

[[nodiscard]] const EditorSceneViewportTextLabel* FindLabel(const ProfilerOverlayContent& content, std::string_view text) {
    const auto found = std::ranges::find(content.labels, text, &EditorSceneViewportTextLabel::text);
    return found == content.labels.end() ? nullptr : &*found;
}

[[nodiscard]] bool SameColor(const std::array<std::uint8_t, 4>& rgba, EditorColor color) {
    return rgba[0] == color.r && rgba[1] == color.g && rgba[2] == color.b;
}

void RunSelectionRoundTripTest() {
    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
    const std::filesystem::path root = TempRoot();
    const std::filesystem::path path = EditorConfigurationStore::FilePath(root);
    std::filesystem::create_directories(path.parent_path(), error);

    std::string saveError;
    Require(EditorConfigurationStore::Save(path, root, EditorConfiguration{}, saveError), "Default settings must save");
    Require(EditorConfigurationStore::Load(path, root).configuration.profiler == ProfilerSelection{}, "The default choice must survive a save and a load");

    EditorConfiguration written;
    written.profiler.areas = { "gpu", "ecs" };
    written.profiler.panelCounters = { "gpu_ms", "ecs_entities", "System Movement" };
    written.profiler.sceneCounters = { "fps", "draw_calls" };
    written.profiler.overlay = ProfilerOverlayOptions{
        .listVisible = true, .listCorner = ProfilerCorner::BottomLeft, .graphVisible = true,
        .graphCorner = ProfilerCorner::TopLeft, .textSize = ProfilerTextSize::Large, .refreshPerSecond = 10U,
        .opacityPercent = 85U, .lines = ProfilerGraphLines{ .frame = false, .renderThread = true, .gpu = false, .budget = false, .alarm = true } };
    Require(EditorConfigurationStore::Save(path, root, written, saveError), "A choice must save");
    const EditorConfigurationLoadResult loaded = EditorConfigurationStore::Load(path, root);
    Require(loaded.Succeeded() && loaded.configuration.profiler == written.profiler, "Both lists and every option must come back as written");

    // Hand edits: a refresh rate and an opacity the window does not offer, a size past the last.
    std::string text;
    {
        std::ifstream input{ path, std::ios::binary };
        text.assign(std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{});
    }
    for (const auto& [from, to] : { std::pair<std::string_view, std::string_view>{ "RefreshPerSecond=10", "RefreshPerSecond=7" },
             { "OpacityPercent=85", "OpacityPercent=0" }, { "TextSize=2", "TextSize=9" } }) {
        const std::size_t at = text.find(from);
        Require(at != std::string::npos, "The saved choice must carry the key");
        text.replace(at, from.size(), to);
    }
    {
        std::ofstream output{ path, std::ios::binary | std::ios::trunc };
        output << text;
    }
    const EditorConfigurationLoadResult edited = EditorConfigurationStore::Load(path, root);
    const ProfilerOverlayOptions fallback;
    Require(edited.Succeeded() && edited.configuration.profiler.overlay.refreshPerSecond == fallback.refreshPerSecond
        && edited.configuration.profiler.overlay.opacityPercent == fallback.opacityPercent, "A value the window does not offer must leave the default");
    Require(edited.configuration.profiler.overlay.textSize == ProfilerTextSize::Large, "A number past the last size must stop at the last size");
    std::filesystem::remove_all(TempRoot(), error);
}

void RunPresetsTest() {
    ProfilerRegistry registry;
    ProfilerSelection selection;
    selection.ApplyPreset(ProfilerPreset::Rendering, registry);
    Require(selection.AreaMeasured(ProfilerArea::Render) && !selection.AreaMeasured(ProfilerArea::Memory), "Rendering measures rendering and not memory");
    Require(selection.OnScene("draw_calls") && selection.InPanel("triangles") && !selection.OnScene("process_memory_mb"), "Rendering shows the draw counters");
    Require(selection.overlay.listVisible && selection.overlay.graphVisible, "A preset turns the scene's list and graph on");

    const auto custom = registry.RegisterCustom("System Movement", ProfilerArea::Ecs, kb::core::ProfilerUnit::Milliseconds);
    Require(custom.has_value(), "A custom counter registers");
    selection.ApplyPreset(ProfilerPreset::Everything, registry);
    Require(selection.OnScene("System Movement") && selection.OnScene("fps") && selection.AreaMeasured(ProfilerArea::Ecs), "Everything includes the counters added at run time");

    selection.SetOnScene("fps", false);
    selection.SetOnScene("fps", false);
    selection.SetInPanel("fps", true);
    selection.SetInPanel("fps", true);
    Require(!selection.OnScene("fps") && selection.InPanel("fps") && std::ranges::count(selection.panelCounters, "fps") == 1, "A counter is listed once and the two lists are independent");
}

void RunOverlayModelTest() {
    ProfilerRegistry registry;
    ProfilerSelection selection;
    selection.areas = { "cpu", "gpu" };
    selection.sceneCounters = { "fps", "frame_ms", "gpu_ms", "draw_calls", "triangles" };
    selection.overlay.listVisible = true;
    Publish(registry, ProfilerBuiltIn::FramesPerSecond, 62.4);
    Publish(registry, ProfilerBuiltIn::FrameTime, 40.0);
    Publish(registry, ProfilerBuiltIn::DrawCalls, 214.0);
    Publish(registry, ProfilerBuiltIn::Triangles, 7'800'000.0);

    const ProfilerOverlayModel model = BuildProfilerOverlayModel(registry, selection);
    const ProfilerOverlayLine* fps = FindLine(model, "FPS");
    const ProfilerOverlayLine* frame = FindLine(model, "Frame");
    const ProfilerOverlayLine* gpu = FindLine(model, "GPU");
    Require(fps && fps->value == "62.4" && fps->severity == ProfilerSeverity::Normal, "A selected counter shows its value, normal above the frame-rate budget");
    Require(frame && frame->value == "40.0 ms" && frame->severity == ProfilerSeverity::Alarm, "40 ms is past the 33.3 ms alarm");
    Require(gpu && !gpu->available && gpu->value.empty(), "A counter nobody published is unavailable, not zero");
    Require(!FindLine(model, "Draws") && !FindLine(model, "Tris"), "Counters of an area that is not measured are not shown, selected or not");
    Require(!FindLine(model, "Views") && !FindLine(model, "Mem"), "Counters that are not selected are not shown");
    Require(model.series.empty(), "The graph is off while the options leave it off");

    selection.areas.push_back("render");
    Publish(registry, ProfilerBuiltIn::FrameTime, 20.0);
    const ProfilerOverlayModel measured = BuildProfilerOverlayModel(registry, selection);
    Require(FindLine(measured, "Draws") && FindLine(measured, "Draws")->value == "214", "Measuring the area shows its counters");
    Require(FindLine(measured, "Tris") && FindLine(measured, "Tris")->value == "7.8 M", "Large counts are shortened");
    Require(FindLine(measured, "Frame")->severity == ProfilerSeverity::Warning, "20 ms is past the 16.6 ms budget only");

    selection.overlay.listVisible = false;
    Require(BuildProfilerOverlayModel(registry, selection).lines.empty(), "The list is empty while the options turn it off");
}

void RunOverlayGraphModelTest() {
    ProfilerRegistry registry;
    ProfilerSelection selection;
    selection.overlay.graphVisible = true;
    selection.overlay.lines.renderThread = true;
    for (int frame = 0; frame < 120; ++frame) {
        Publish(registry, ProfilerBuiltIn::FrameTime, 10.0 + frame % 5);
        Publish(registry, ProfilerBuiltIn::GpuTime, 8.0);
    }
    const ProfilerOverlayModel model = BuildProfilerOverlayModel(registry, selection);
    Require(model.series.size() == 2U, "Only the lines that have readings are drawn: the render thread has none");
    Require(model.series[0].role == ProfilerSeriesRole::Frame && model.series[0].samples.size() == 120U && model.series[1].role == ProfilerSeriesRole::Gpu, "Frame first, then GPU");
    Require(model.budgetMs == 16.6 && model.alarmMs == 33.3, "The thresholds are the frame counter's");
    Require(model.axisTopMs == 40.0, "The axis reaches 1.2 times the alarm, in whole 5 ms steps");
    Require(model.spanSeconds > 1.43 && model.spanSeconds < 1.45, "120 frames of 12 ms on average cover 1.44 s");

    selection.overlay.lines.budget = false;
    selection.overlay.lines.gpu = false;
    const ProfilerOverlayModel fewer = BuildProfilerOverlayModel(registry, selection);
    Require(fewer.series.size() == 1U && fewer.budgetMs == 0.0 && fewer.alarmMs == 33.3, "A line the options turn off is not in the model");
}

void RunOverlayLayoutTest() {
    const EditorTheme theme = MakeEditorDarkTheme();
    ProfilerRegistry registry;
    ProfilerSelection selection;
    selection.overlay.listVisible = true;
    selection.overlay.graphVisible = true;
    selection.areas = { "cpu", "gpu" };
    Publish(registry, ProfilerBuiltIn::FramesPerSecond, 20.0);
    for (int frame = 0; frame < 240; ++frame) Publish(registry, ProfilerBuiltIn::FrameTime, 50.0);
    const ProfilerOverlayModel model = BuildProfilerOverlayModel(registry, selection);
    ProfilerOverlayContent content;
    AppendProfilerOverlay(content, model, selection.overlay, theme, ProfilerOverlayMetrics::ForTextSize(ProfilerTextSize::Small), 1337.0F, 640.0F);

    const auto* fpsValue = FindLabel(content, "20.0");
    const auto* frameValue = FindLabel(content, "50.0 ms");
    const auto* gpuValue = FindLabel(content, "unavailable");
    Require(fpsValue && SameColor(fpsValue->color, profiler_palette::kAlarm), "20 fps is under the 30 fps alarm: the value is colored by its threshold");
    Require(frameValue && SameColor(frameValue->color, profiler_palette::kAlarm), "50 ms is colored as an alarm");
    Require(gpuValue && SameColor(gpuValue->color, theme.textDisabled), "An unavailable counter says so, dimmed");
    Require(FindLabel(content, "FPS") && SameColor(FindLabel(content, "FPS")->color, theme.textSecondary), "A counter's name is the secondary text color");
    Require(fpsValue->align == EditorSceneViewportTextAlign::Right && fpsValue->x > FindLabel(content, "FPS")->x, "Values line up at the right edge of the list");

    int dashed = 0;
    int polylines = 0;
    int panels = 0;
    for (const auto& shape : content.shapes) {
        if (shape.kind == EditorSceneViewportOverlayShape::Kind::RoundedRect && shape.points.size() == 2U && shape.radius >= 4.0F) ++panels;
        if (shape.kind == EditorSceneViewportOverlayShape::Kind::Polyline && shape.dashed) ++dashed;
        if (shape.kind == EditorSceneViewportOverlayShape::Kind::Polyline && !shape.dashed && shape.points.size() == 240U) ++polylines;
    }
    Require(panels >= 2, "The list and the graph each sit on a panel");
    Require(dashed == 2, "The budget and the alarm are dashed lines");
    Require(polylines == 1, "One polyline per line with readings, one point per reading");
    Require(FindLabel(content, "16.6 ms (budget)") && FindLabel(content, "33.3 ms (alarm)"), "The threshold lines are named with their value");

    // The right edge of the graph is the newest reading, inside the viewport.
    const auto& line = *std::ranges::find_if(content.shapes, [](const auto& shape) { return shape.points.size() == 240U; });
    Require(line.points.back()[0] < 1337.0F && line.points.back()[1] >= 0.0F && line.points.back()[1] < 640.0F, "The graph stays inside the viewport");

    // The corner decides where the list sits.
    ProfilerOverlayContent left;
    ProfilerOverlayOptions options = selection.overlay;
    options.listCorner = ProfilerCorner::TopLeft;
    AppendProfilerOverlay(left, model, options, theme, ProfilerOverlayMetrics::ForTextSize(ProfilerTextSize::Small), 1337.0F, 640.0F);
    Require(FindLabel(left, "FPS")->x < FindLabel(content, "FPS")->x, "The top left corner puts the list left of the top right corner's");

    selection.overlay.listVisible = false;
    selection.overlay.graphVisible = false;
    ProfilerOverlayContent empty;
    AppendProfilerOverlay(empty, BuildProfilerOverlayModel(registry, selection), selection.overlay, theme, ProfilerOverlayMetrics{}, 1337.0F, 640.0F);
    Require(empty.labels.empty() && empty.shapes.empty(), "Nothing is drawn while the options turn the list and the graph off");
}

} // namespace

namespace kb::editor::tests {

void RunEditorProfilerTests() {
    RunSelectionRoundTripTest();
    RunPresetsTest();
    RunOverlayModelTest();
    RunOverlayGraphModelTest();
    RunOverlayLayoutTest();
}

} // namespace kb::editor::tests
