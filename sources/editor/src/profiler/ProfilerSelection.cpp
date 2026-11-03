#include "profiler/ProfilerSelection.hpp"

#include "engine/config/IniDocument.hpp"

#include <algorithm>
#include <span>

namespace kb::editor {
namespace {

using kb::core::ProfilerArea;

constexpr std::string_view kSection = "Editor.Profiler";

[[nodiscard]] bool Has(const std::vector<std::string>& list, std::string_view id) noexcept {
    return std::ranges::find(list, id) != list.end();
}

void Set(std::vector<std::string>& list, std::string_view id, bool present) {
    const auto found = std::ranges::find(list, id);
    if (present && found == list.end()) list.emplace_back(id);
    if (!present && found != list.end()) list.erase(found);
}

[[nodiscard]] std::string Join(const std::vector<std::string>& list) {
    std::string text;
    for (const std::string& item : list) {
        if (!text.empty()) text += ',';
        text += item;
    }
    return text;
}

[[nodiscard]] std::vector<std::string> Split(std::string_view text) {
    std::vector<std::string> items;
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        const std::string_view item = text.substr(0U, comma);
        if (!item.empty()) items.emplace_back(item);
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1U);
    }
    return items;
}

template <typename Enum>
[[nodiscard]] Enum ReadEnum(const kb::config::IniDocument& document, std::string_view key, Enum fallback, int last) {
    const std::int64_t value = document.GetInt(kSection, key).value_or(static_cast<std::int64_t>(fallback));
    return static_cast<Enum>(std::clamp<std::int64_t>(value, 0, last));
}

// A value outside the choices the window offers is a hand edit: keep the default.
[[nodiscard]] std::uint8_t ReadChoice(const kb::config::IniDocument& document, std::string_view key,
    std::uint8_t fallback, std::span<const std::uint8_t> choices) {
    const std::int64_t value = document.GetInt(kSection, key).value_or(fallback);
    return std::ranges::find(choices, value) != choices.end() ? static_cast<std::uint8_t>(value) : fallback;
}

} // namespace

// First use shows the frame counters of the cheap areas, and nothing on the scene until asked.
ProfilerSelection::ProfilerSelection()
    : areas{ "cpu", "gpu" },
      panelCounters{ "fps", "frame_ms", "gpu_ms" },
      sceneCounters{ "fps", "frame_ms", "gpu_ms" } {}

bool ProfilerSelection::AreaMeasured(ProfilerArea area) const noexcept { return Has(areas, AreaId(area)); }
bool ProfilerSelection::InPanel(std::string_view counterId) const noexcept { return Has(panelCounters, counterId); }
bool ProfilerSelection::OnScene(std::string_view counterId) const noexcept { return Has(sceneCounters, counterId); }
void ProfilerSelection::SetAreaMeasured(ProfilerArea area, bool measured) { Set(areas, AreaId(area), measured); }
void ProfilerSelection::SetInPanel(std::string_view counterId, bool shown) { Set(panelCounters, counterId, shown); }
void ProfilerSelection::SetOnScene(std::string_view counterId, bool shown) { Set(sceneCounters, counterId, shown); }

std::string_view ProfilerSelection::AreaId(ProfilerArea area) noexcept {
    switch (area) {
    case ProfilerArea::Cpu: return "cpu";
    case ProfilerArea::Gpu: return "gpu";
    case ProfilerArea::Render: return "render";
    case ProfilerArea::Ecs: return "ecs";
    case ProfilerArea::Memory: return "memory";
    }
    return {};
}

void ProfilerSelection::ApplyPreset(ProfilerPreset preset, const kb::core::ProfilerRegistry& registry) {
    std::vector<std::string> counters;
    std::vector<ProfilerArea> measured;
    switch (preset) {
    case ProfilerPreset::Minimal:
        measured = { ProfilerArea::Cpu, ProfilerArea::Gpu };
        counters = { "fps", "frame_ms", "gpu_ms" };
        break;
    case ProfilerPreset::Rendering:
        measured = { ProfilerArea::Cpu, ProfilerArea::Gpu, ProfilerArea::Render };
        counters = { "fps", "frame_ms", "render_thread_ms", "gpu_ms", "draw_calls", "triangles", "views" };
        break;
    case ProfilerPreset::Memory:
        measured = { ProfilerArea::Cpu, ProfilerArea::Memory };
        counters = { "fps", "frame_ms", "process_memory_mb", "gpu_memory_mb", "texture_memory_mb", "render_target_memory_mb" };
        break;
    case ProfilerPreset::Everything:
        measured = { ProfilerArea::Cpu, ProfilerArea::Gpu, ProfilerArea::Render, ProfilerArea::Ecs, ProfilerArea::Memory };
        for (std::size_t index = 0U; index < registry.CounterCount(); ++index) {
            counters.emplace_back(registry.Info(index).id);
        }
        break;
    }
    areas.clear();
    for (const ProfilerArea area : measured) areas.emplace_back(AreaId(area));
    panelCounters = counters;
    sceneCounters = std::move(counters);
    overlay.listVisible = true;
    overlay.graphVisible = true;
}

ProfilerSelection ProfilerSelection::Read(const kb::config::IniDocument& document) {
    ProfilerSelection selection;
    const auto list = [&](std::string_view key, std::vector<std::string>& target) {
        if (const auto text = document.GetString(kSection, key)) target = Split(*text);
    };
    list("Areas", selection.areas);
    list("PanelCounters", selection.panelCounters);
    list("SceneCounters", selection.sceneCounters);
    ProfilerOverlayOptions& options = selection.overlay;
    options.listVisible = document.GetBool(kSection, "ListVisible").value_or(options.listVisible);
    options.graphVisible = document.GetBool(kSection, "GraphVisible").value_or(options.graphVisible);
    options.listCorner = ReadEnum(document, "ListCorner", options.listCorner, 3);
    options.graphCorner = ReadEnum(document, "GraphCorner", options.graphCorner, 3);
    options.textSize = ReadEnum(document, "TextSize", options.textSize, 2);
    options.refreshPerSecond = ReadChoice(document, "RefreshPerSecond", options.refreshPerSecond, ProfilerOverlayOptions::RefreshChoices());
    options.opacityPercent = ReadChoice(document, "OpacityPercent", options.opacityPercent, ProfilerOverlayOptions::OpacityChoices());
    options.lines.frame = document.GetBool(kSection, "LineFrame").value_or(options.lines.frame);
    options.lines.renderThread = document.GetBool(kSection, "LineRenderThread").value_or(options.lines.renderThread);
    options.lines.gpu = document.GetBool(kSection, "LineGpu").value_or(options.lines.gpu);
    options.lines.budget = document.GetBool(kSection, "LineBudget").value_or(options.lines.budget);
    options.lines.alarm = document.GetBool(kSection, "LineAlarm").value_or(options.lines.alarm);
    return selection;
}

void ProfilerSelection::Write(kb::config::IniDocument& document) const {
    document.SetString(kSection, "Areas", Join(areas));
    document.SetString(kSection, "PanelCounters", Join(panelCounters));
    document.SetString(kSection, "SceneCounters", Join(sceneCounters));
    document.SetBool(kSection, "ListVisible", overlay.listVisible);
    document.SetBool(kSection, "GraphVisible", overlay.graphVisible);
    document.SetInt(kSection, "ListCorner", static_cast<std::int64_t>(overlay.listCorner));
    document.SetInt(kSection, "GraphCorner", static_cast<std::int64_t>(overlay.graphCorner));
    document.SetInt(kSection, "TextSize", static_cast<std::int64_t>(overlay.textSize));
    document.SetInt(kSection, "RefreshPerSecond", overlay.refreshPerSecond);
    document.SetInt(kSection, "OpacityPercent", overlay.opacityPercent);
    document.SetBool(kSection, "LineFrame", overlay.lines.frame);
    document.SetBool(kSection, "LineRenderThread", overlay.lines.renderThread);
    document.SetBool(kSection, "LineGpu", overlay.lines.gpu);
    document.SetBool(kSection, "LineBudget", overlay.lines.budget);
    document.SetBool(kSection, "LineAlarm", overlay.lines.alarm);
}

} // namespace kb::editor
