#include "profiler/ProfilerText.hpp"

#include "localization/EditorLocalization.hpp"

#include <format>
#include <initializer_list>
#include <utility>

namespace kb::editor {
namespace {

using Values = std::initializer_list<std::pair<std::string_view, std::string>>;

[[nodiscard]] std::string One(double value) { return std::format("{:.1f}", value); }
[[nodiscard]] std::string Whole(double value) { return std::format("{:.0f}", value); }
[[nodiscard]] std::string Words(std::string_view text) { return std::string{ text }; }

} // namespace

std::string_view ProfilerText::Region(std::string_view name) {
    if (name == "Editor frame") return EditorText("profiler.region.frame", "Editor frame");
    if (name == "Game tick") return EditorText("profiler.region.game_tick", "Game tick");
    if (name == "Scene update") return EditorText("profiler.region.scene_update", "Scene update");
    if (name == "Transform sync") return EditorText("profiler.region.transform_sync", "Transform sync");
    if (name == "Present") return EditorText("profiler.region.present", "Present");
    if (name == "Begin layout") return EditorText("profiler.region.begin_layout", "Begin layout");
    if (name == "Queue main window") return EditorText("profiler.region.queue_main", "Queue main window");
    if (name == "Queue floating windows") return EditorText("profiler.region.queue_floating", "Queue floating windows");
    if (name == "End frame") return EditorText("profiler.region.end_frame", "End frame");
    return name;
}

std::string_view ProfilerText::Severity(kb::core::DiagnosisSeverity severity) {
    switch (severity) {
    case kb::core::DiagnosisSeverity::Problem: return EditorText("profiler.severity.problem", "Problem");
    case kb::core::DiagnosisSeverity::Warning: return EditorText("profiler.severity.warning", "Warning");
    case kb::core::DiagnosisSeverity::Tip: return EditorText("profiler.severity.tip", "Tip");
    }
    return {};
}

std::string_view ProfilerText::Category(kb::core::DiagnosisCategory category) {
    switch (category) {
    case kb::core::DiagnosisCategory::Cpu: return EditorText("profiler.category.cpu", "CPU");
    case kb::core::DiagnosisCategory::Gpu: return EditorText("profiler.category.gpu", "GPU");
    case kb::core::DiagnosisCategory::Memory: return EditorText("profiler.category.memory", "Memory");
    case kb::core::DiagnosisCategory::Frame: return EditorText("profiler.category.frame", "Frame");
    }
    return {};
}

std::string ProfilerText::FindingTitle(const kb::core::DiagnosisFinding& finding, const kb::core::ProfilerRegistry&) {
    if (finding.rule == "frame_over_budget") {
        return EditorLocalization::Format("profiler.finding.frame_over_budget.title", "The frame takes {value} ms, over the {limit} ms budget",
            Values{ { "value", One(finding.value) }, { "limit", One(finding.limit) } });
    }
    if (finding.rule == "draw_calls_high") {
        return EditorLocalization::Format("profiler.finding.draw_calls_high.title", "{value} draw calls a frame, over the {limit} threshold",
            Values{ { "value", Whole(finding.value) }, { "limit", Whole(finding.limit) } });
    }
    if (finding.rule == "triangles_high") {
        return EditorLocalization::Format("profiler.finding.triangles_high.title", "{value} thousand triangles a frame, over the {limit} thousand threshold",
            Values{ { "value", Whole(finding.value / 1000.0) }, { "limit", Whole(finding.limit / 1000.0) } });
    }
    if (finding.rule == "memory_growth") {
        return EditorLocalization::Format("profiler.finding.memory_growth.title", "Memory grew by {value} MB over {limit} frames",
            Values{ { "value", Whole(finding.value) }, { "limit", Whole(finding.limit) } });
    }
    if (finding.rule == "hot_region") {
        return EditorLocalization::Format("profiler.finding.hot_region.title", "{subject} takes {value}% of the frame",
            Values{ { "subject", Words(Region(finding.subject)) }, { "value", Whole(finding.value) } });
    }
    return EditorLocalization::Format("profiler.finding.frame_spikes.title", "{value} frames cost more than {limit} ms",
        Values{ { "value", Whole(finding.value) }, { "limit", One(finding.limit) } });
}

std::string ProfilerText::FindingDetail(const kb::core::DiagnosisFinding& finding) {
    if (finding.rule == "frame_over_budget") {
        if (finding.subject == "gpu") return Words(EditorText("profiler.finding.frame_over_budget.gpu", "The graphics card takes most of each frame. Lower what it draws: fewer triangles, cheaper materials, smaller shadows."));
        if (finding.subject == "game") return Words(EditorText("profiler.finding.frame_over_budget.game", "The game thread takes most of each frame. Look for the largest regions under Game tick in Measurements."));
        return Words(EditorText("profiler.finding.frame_over_budget.frame", "Neither the graphics card nor the game thread explains it alone. Select the slowest frame in Measurements and read its hierarchy."));
    }
    if (finding.rule == "draw_calls_high") return Words(EditorText("profiler.finding.draw_calls_high.detail", "Every draw call costs the CPU time to prepare and submit. Many small objects with the same mesh and material can be drawn together."));
    if (finding.rule == "triangles_high") return Words(EditorText("profiler.finding.triangles_high.detail", "More triangles than the target can draw in time. The cost lands on the graphics card."));
    if (finding.rule == "memory_growth") return Words(EditorText("profiler.finding.memory_growth.detail", "Process memory keeps rising instead of settling. Something is created every frame and never released."));
    if (finding.rule == "hot_region") {
        return EditorLocalization::Format("profiler.finding.hot_region.detail", "This region holds more than {limit}% of its frame, and {gain} ms of that is its own work.",
            Values{ { "limit", Whole(finding.limit) }, { "gain", One(finding.gainMilliseconds) } });
    }
    return Words(EditorText("profiler.finding.frame_spikes.detail", "A few frames are far slower than their neighbours, which is felt as stutter."));
}

std::vector<std::string> ProfilerText::FindingSteps(const kb::core::DiagnosisFinding& finding) {
    if (finding.rule == "frame_over_budget") {
        return { Words(EditorText("profiler.finding.frame_over_budget.step1", "Open Measurements and select a frame over the budget.")),
                 Words(EditorText("profiler.finding.frame_over_budget.step2", "Read its hierarchy: the region with the most time of its own is the one to fix first.")) };
    }
    if (finding.rule == "draw_calls_high") {
        return { Words(EditorText("profiler.finding.draw_calls_high.step1", "Merge meshes that share a material, or draw them as instances.")),
                 Words(EditorText("profiler.finding.draw_calls_high.step2", "Compare the Visible meshes counter with the draw calls: they should be close.")) };
    }
    if (finding.rule == "triangles_high") {
        return { Words(EditorText("profiler.finding.triangles_high.step1", "Use simpler levels of detail for objects that are far away.")),
                 Words(EditorText("profiler.finding.triangles_high.step2", "Remove geometry the camera never sees.")) };
    }
    if (finding.rule == "memory_growth") {
        return { Words(EditorText("profiler.finding.memory_growth.step1", "Watch Process memory while the scene runs without changes.")),
                 Words(EditorText("profiler.finding.memory_growth.step2", "Look for objects, textures or buffers that are created in a loop.")) };
    }
    if (finding.rule == "hot_region") {
        return { Words(EditorText("profiler.finding.hot_region.step1", "Select this frame in Measurements and open the region in the hierarchy.")),
                 Words(EditorText("profiler.finding.hot_region.step2", "Do less work in it, or do it less often.")) };
    }
    return { Words(EditorText("profiler.finding.frame_spikes.step1", "Select a spike in the Frame chart and read what its hierarchy holds.")),
             Words(EditorText("profiler.finding.frame_spikes.step2", "Look for loading, allocation or a script that runs only now and then.")) };
}

std::string ProfilerText::FindingWhere(const kb::core::DiagnosisFinding& finding, const kb::core::ProfilerRegistry& registry) {
    if (finding.rule == "hot_region") return Words(Region(finding.subject));
    if (finding.subject == "gpu") return Words(EditorText("profiler.where.gpu", "Graphics card"));
    if (finding.subject == "game") return Words(EditorText("profiler.where.game", "Game thread"));
    if (finding.subject == "frame") return Words(EditorText("profiler.where.frame", "Whole frame"));
    if (const std::optional<std::size_t> counter = registry.Find(finding.subject)) return Words(Counter(registry.Info(*counter)));
    return finding.subject;
}

} // namespace kb::editor
