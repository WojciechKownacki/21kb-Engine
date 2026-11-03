#include "profiler/ProfilerPanel.hpp"

#if defined(_WIN32)
#include "localization/EditorLocalization.hpp"
#include "profiler/ProfilerPalette.hpp"
#include "profiler/ProfilerPanelLayout.hpp"
#include "profiler/ProfilerText.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/components/EditorDialogStyle.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numeric>

namespace kb::editor {
namespace {

using kb::core::ProfilerArea;
using kb::core::ProfilerBuiltIn;
using kb::core::ProfilerRegistry;
using kb::core::ProfilerSeverity;
namespace palette = profiler_palette;

constexpr std::size_t kHistory = ProfilerRegistry::HistoryFrames();
constexpr int kPad = 10;
constexpr int kFindingHeight = ProfilerPanelGeometry::kFindingHeight;
constexpr std::array<ProfilerArea, 5> kAreas{
    ProfilerArea::Cpu, ProfilerArea::Gpu, ProfilerArea::Render, ProfilerArea::Ecs, ProfilerArea::Memory };

struct Canvas {
    HDC dc;
    const EditorTheme& theme;

    [[nodiscard]] static COLORREF C(EditorColor color) noexcept { return GdiDrawing::ToColorRef(color); }
    void Fill(const RECT& rect, EditorColor color) const { GdiDrawing::FillRectColor(dc, rect, C(color)); }
    void Text(RECT rect, std::string_view text, EditorColor color, int size = 11, int weight = FW_NORMAL,
        UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) const {
        EditorDialogStyle::PaintText(dc, rect, text, C(color), size, weight, format);
    }
    void TextRight(RECT rect, std::string_view text, EditorColor color, int size = 11, int weight = FW_NORMAL) const {
        Text(rect, text, color, size, weight, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    void Stroke(const std::vector<POINT>& points, EditorColor color, int style = PS_SOLID, int width = 1) const {
        if (points.size() < 2U) return;
        HPEN pen = CreatePen(style, width, C(color));
        HGDIOBJ previous = SelectObject(dc, pen);
        Polyline(dc, points.data(), static_cast<int>(points.size()));
        SelectObject(dc, previous);
        DeleteObject(pen);
    }
    void HLine(int left, int right, int y, EditorColor color, int style = PS_SOLID) const { Stroke({ { left, y }, { right, y } }, color, style); }
    void Pill(const RECT& rect, std::string_view text, EditorColor foreground, EditorColor background) const {
        HBRUSH brush = CreateSolidBrush(C(background));
        HPEN pen = CreatePen(PS_SOLID, 1, C(background));
        HGDIOBJ previousBrush = SelectObject(dc, brush);
        HGDIOBJ previousPen = SelectObject(dc, pen);
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, rect.bottom - rect.top, rect.bottom - rect.top);
        SelectObject(dc, previousPen);
        SelectObject(dc, previousBrush);
        DeleteObject(pen);
        DeleteObject(brush);
        Text(rect, text, foreground, 10, FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    // A color of the theme blended toward another, for fills that sit between two tokens.
    [[nodiscard]] EditorColor Mix(EditorColor first, EditorColor second, int secondPercent) const noexcept {
        const auto channel = [&](std::uint8_t a, std::uint8_t b) { return static_cast<std::uint8_t>((a * (100 - secondPercent) + b * secondPercent) / 100); };
        return EditorColor{ channel(first.r, second.r), channel(first.g, second.g), channel(first.b, second.b) };
    }
};

[[nodiscard]] RECT Rect(int left, int top, int right, int bottom) noexcept { return RECT{ left, top, right, bottom }; }
[[nodiscard]] RECT Inset(RECT rect, int dx, int dy) noexcept { return RECT{ rect.left + dx, rect.top + dy, rect.right - dx, rect.bottom - dy }; }

[[nodiscard]] std::vector<double> Series(const ProfilerRegistry& registry, ProfilerBuiltIn counter) {
    std::array<double, kHistory> samples{};
    const std::size_t count = registry.Samples(ProfilerRegistry::Id(counter), samples);
    return { samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(count) };
}

[[nodiscard]] std::string ValueText(const ProfilerRegistry& registry, std::size_t counter) {
    const kb::core::ProfilerCounterStats stats = registry.Stats(counter);
    return stats.available ? FormatProfilerValue(registry.Info(counter), stats.now) : std::string{ "-" };
}

[[nodiscard]] EditorColor SeverityColor(const EditorTheme& theme, ProfilerSeverity severity) {
    return severity == ProfilerSeverity::Alarm ? palette::kAlarm : severity == ProfilerSeverity::Warning ? palette::kWarning : theme.textPrimary;
}

// The value a counter has now, with a color that says how it stands against its thresholds.
struct Reading {
    bool available = false;
    std::string text;
    EditorColor color;
};
[[nodiscard]] Reading ReadingOf(const ProfilerRegistry& registry, std::size_t counter, const EditorTheme& theme) {
    const kb::core::ProfilerCounterStats stats = registry.Stats(counter);
    if (!stats.available) return { false, "-", theme.textDisabled };
    return { true, FormatProfilerValue(registry.Info(counter), stats.now), SeverityColor(theme, ProfilerRegistry::Severity(registry.Info(counter), stats.now)) };
}

[[nodiscard]] std::string ThresholdText(const kb::core::ProfilerCounterInfo& info, double value, bool alarm) {
    if (value <= 0.0) return "-";
    const std::string number = value >= 1'000'000.0 ? std::format("{:g} M", value / 1'000'000.0) : std::format("{:g}", value);
    return info.lowerIsWorse ? (alarm ? "< " : ">= ") + number : number;
}

[[nodiscard]] EditorColor AreaColor(const kb::core::ProfilerCounterInfo& info) { return palette::Area(info.area); }

[[nodiscard]] std::string AreaHeadline(const ProfilerRegistry& registry, ProfilerArea area) {
    const ProfilerBuiltIn counter = area == ProfilerArea::Cpu ? ProfilerBuiltIn::FrameTime : area == ProfilerArea::Gpu ? ProfilerBuiltIn::GpuTime
        : area == ProfilerArea::Render ? ProfilerBuiltIn::DrawCalls : area == ProfilerArea::Ecs ? ProfilerBuiltIn::EcsEntities : ProfilerBuiltIn::ProcessMemory;
    return ValueText(registry, ProfilerRegistry::Id(counter));
}

class PanelPainter {
public:
    PanelPainter(HDC dc, const EditorTheme& theme, const EditorProfilerState& profiler, const ProfilerSelection& selection,
        const ProfilerPanelGeometry& geometry)
        : canvas_{ dc, theme }, theme_(theme), profiler_(profiler), selection_(selection), geometry_(geometry), registry_(profiler.Registry()) {}

    void Paint() {
        const Canvas& c = canvas_;
        c.Fill(geometry_.content, theme_.panel);
        PaintStrips();
        if (profiler_.Panel().tab == ProfilerPanelTab::Diagnosis) {
            PaintDiagnosisLeft();
            PaintDiagnosis();
            if (geometry_.right.right > geometry_.right.left) PaintDiagnosisDetails();
        } else {
            PaintLeft();
            if (profiler_.Panel().tab == ProfilerPanelTab::Measurements) PaintMeasurements(); else PaintCounters();
        }
        PaintStatus();
    }

private:
    Canvas canvas_;
    const EditorTheme& theme_;
    const EditorProfilerState& profiler_;
    const ProfilerSelection& selection_;
    const ProfilerPanelGeometry& geometry_;
    const ProfilerRegistry& registry_;

    void Clip(const RECT& rect) const { IntersectClipRect(canvas_.dc, rect.left, rect.top, rect.right, rect.bottom); }

    [[nodiscard]] std::optional<std::size_t> ShownOffset() const {
        const std::optional<std::uint64_t> shown = ProfilerShownFrame(profiler_);
        return shown ? std::optional<std::size_t>{ static_cast<std::size_t>(profiler_.Frame() - *shown) } : std::nullopt;
    }

    void PaintStrips() const {
        const Canvas& c = canvas_;
        c.Fill(geometry_.tabs, theme_.chrome);
        c.Fill(geometry_.toolbar, theme_.toolbar);
        c.Fill(Rect(geometry_.toolbar.left, geometry_.toolbar.bottom - 1, geometry_.toolbar.right, geometry_.toolbar.bottom), theme_.borderChrome);
        const std::vector<ProfilerPanelButton> buttons = ProfilerPanelButtons(geometry_, profiler_, selection_);
        const std::size_t visibleFindings = ProfilerVisibleFindings(profiler_).size();
        for (const ProfilerPanelButton& button : buttons) {
            using Action = ProfilerPanelAction;
            const RECT& r = button.hit.bounds;
            switch (button.hit.action) {
            case Action::Tab:
                c.Fill(r, button.active ? theme_.tabActive : theme_.tabInactive);
                c.Text(Rect(r.left + 14, r.top, r.right - (button.hit.argument == 2 ? 30 : 8), r.bottom), button.text,
                    button.active ? theme_.textPrimary : theme_.textSecondary, 12, FW_SEMIBOLD);
                if (button.hit.argument == 2 && visibleFindings > 0U) {
                    c.Pill(Rect(r.right - 30, r.top + 7, r.right - 8, r.top + 23), std::to_string(visibleFindings), palette::kWarning, c.Mix(theme_.panel, palette::kWarning, 22));
                }
                break;
            case Action::AreasAll: case Action::AreasNone: case Action::CountersAll: case Action::CountersNone:
                break;  // drawn with the left pane, which would cover them here
            case Action::Record:
                EditorDialogStyle::PaintButton(canvas_.dc, r, theme_, button.text, button.active ? EditorDialogButtonTone::Destructive : EditorDialogButtonTone::Neutral);
                if (!button.active) {
                    c.Pill(Rect(r.left + 9, r.top + 9, r.left + 18, r.top + 17), "", palette::kAlarm, palette::kAlarm);
                }
                break;
            case Action::Live:
                // Following the latest frame is a lit pill; the arrow that returns to it is a plain button.
                if (button.hit.argument == 0 && button.active) {
                    c.Pill(r, button.text, palette::kBudget, c.Mix(theme_.panel, palette::kBudget, 18));
                } else {
                    EditorDialogStyle::PaintButton(canvas_.dc, r, theme_, button.text);
                }
                break;
            default:
                EditorDialogStyle::PaintButton(canvas_.dc, r, theme_, button.text, button.active ? EditorDialogButtonTone::Primary : EditorDialogButtonTone::Neutral);
                break;
            }
        }
        // The frame the arrows move through.
        const auto previous = std::ranges::find_if(buttons, [](const ProfilerPanelButton& b) { return b.hit.action == ProfilerPanelAction::PreviousFrame; });
        const auto next = std::ranges::find_if(buttons, [](const ProfilerPanelButton& b) { return b.hit.action == ProfilerPanelAction::NextFrame; });
        if (previous != buttons.end() && next != buttons.end()) {
            const std::optional<std::uint64_t> shown = ProfilerShownFrame(profiler_);
            const std::string text = shown ? EditorLocalization::Format("profiler.panel.frame_of", "Frame {frame} / {total}",
                { { "frame", std::to_string(*shown) }, { "total", std::to_string(profiler_.Frame()) } }) : std::string{ EditorText("profiler.panel.no_frames", "No frames yet") };
            c.Text(Rect(previous->hit.bounds.right + 6, previous->hit.bounds.top, next->hit.bounds.left - 6, previous->hit.bounds.bottom), text, theme_.textPrimary, 11, FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    void PaintGroupHeader(const RECT& rect, std::string_view text) const {
        canvas_.Fill(rect, theme_.chrome);
        canvas_.Text(Rect(rect.left + kPad, rect.top, rect.right - kPad, rect.bottom), text, theme_.textDisabled, 10, FW_SEMIBOLD);
    }

    void PaintSectionHeader(const RECT& rect, std::string_view text) const {
        canvas_.Fill(rect, theme_.strip);
        canvas_.Text(Rect(rect.left + kPad, rect.top, rect.right - kPad, rect.bottom), text, theme_.textPrimary, 12, FW_SEMIBOLD);
    }

    void PaintLeft() const {
        const Canvas& c = canvas_;
        c.Fill(geometry_.left, theme_.strip);
        c.Fill(Rect(geometry_.left.right - 1, geometry_.left.top, geometry_.left.right, geometry_.left.bottom), theme_.borderChrome);
        PaintSectionHeader(geometry_.areasHeader, std::string{ EditorText("profiler.panel.areas", "Areas") });
        PaintSectionHeader(geometry_.countersHeader, std::string{ EditorText("profiler.panel.counters", "Counters") });
        for (const ProfilerPanelButton& button : ProfilerPanelButtons(geometry_, profiler_, selection_)) {
            using Action = ProfilerPanelAction;
            const Action action = button.hit.action;
            if (action == Action::AreasAll || action == Action::AreasNone || action == Action::CountersAll || action == Action::CountersNone) {
                c.TextRight(button.hit.bounds, button.text, theme_.textDisabled, 10);
            }
        }
        // The dot between the two links of each header.
        for (const RECT& header : { geometry_.areasHeader, geometry_.countersHeader }) {
            c.Text(Rect(header.right - 10 - 28 - 14, header.top, header.right - 10 - 28, header.bottom), "\xC2\xB7", theme_.textDisabled, 10, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        for (std::size_t index = 0U; index < kAreas.size(); ++index) {
            const ProfilerArea area = kAreas[index];
            const RECT row = Rect(geometry_.areasList.left, geometry_.areasList.top + static_cast<int>(index) * ProfilerPanelGeometry::kRowHeight,
                geometry_.areasList.right, geometry_.areasList.top + static_cast<int>(index + 1U) * ProfilerPanelGeometry::kRowHeight);
            const bool measured = selection_.AreaMeasured(area);
            EditorDialogStyle::PaintCheckbox(c.dc, Rect(row.left + kPad, row.top + 4, row.left + kPad + 14, row.top + 18), theme_, measured);
            c.Fill(Rect(row.left + kPad + 22, row.top + 7, row.left + kPad + 30, row.top + 15), palette::Area(area));
            c.Text(Rect(row.left + kPad + 38, row.top, row.right - 80, row.bottom), ProfilerText::Area(area), measured ? theme_.textPrimary : theme_.textSecondary);
            c.TextRight(Rect(row.right - 78, row.top, row.right - kPad, row.bottom), measured ? AreaHeadline(registry_, area) : std::string{ "-" }, theme_.textDisabled, 10);
        }
        const RECT list = geometry_.countersList;
        const int saved = SaveDC(c.dc);
        Clip(list);
        int top = list.top - profiler_.Panel().leftScroll;
        for (const ProfilerCounterRow& row : ProfilerCounterRows(registry_)) {
            const kb::core::ProfilerCounterInfo& info = registry_.Info(row.counter);
            if (row.startsGroup) {
                PaintGroupHeader(Rect(list.left, top, list.right - 1, top + ProfilerPanelGeometry::kRowHeight), ProfilerText::Group(info.group));
                top += ProfilerPanelGeometry::kRowHeight;
            }
            const bool measured = selection_.AreaMeasured(info.area);
            const bool shown = selection_.InPanel(info.id);
            const RECT r = Rect(list.left, top, list.right - 1, top + ProfilerPanelGeometry::kRowHeight);
            if (r.bottom > list.top && r.top < list.bottom) {
                EditorDialogStyle::PaintCheckbox(c.dc, Rect(r.left + kPad, r.top + 4, r.left + kPad + 14, r.top + 18), theme_, shown, measured);
                c.Text(Rect(r.left + kPad + 24, r.top, r.right - 84, r.bottom), ProfilerText::Counter(info), measured && shown ? theme_.textPrimary : measured ? theme_.textSecondary : theme_.textDisabled);
                c.TextRight(Rect(r.right - 82, r.top, r.right - kPad, r.bottom), measured ? ValueText(registry_, row.counter) : std::string{ "-" }, theme_.textDisabled, 10);
            }
            top += ProfilerPanelGeometry::kRowHeight;
        }
        RestoreDC(c.dc, saved);
    }

    // The unit-less size of a chart's vertical axis: room above the highest reading and the thresholds.
    [[nodiscard]] static double AxisMaximum(const std::vector<std::vector<double>>& series, double budget, double alarm) {
        double highest = 0.0;
        for (const auto& values : series) {
            for (const double value : values) highest = std::max(highest, value);
        }
        return std::max({ highest * 1.1, alarm * 1.2, budget * 1.2, 1e-6 });
    }

    [[nodiscard]] static int ValueY(const RECT& plot, double value, double axisMaximum) {
        const double fraction = std::clamp(value / axisMaximum, 0.0, 1.0);
        return plot.bottom - 2 - static_cast<int>(fraction * (plot.bottom - plot.top - 4));
    }

    void PaintLine(const RECT& plot, const std::vector<double>& values, double axisMaximum, EditorColor color) const {
        std::vector<POINT> points;
        points.reserve(values.size());
        for (std::size_t index = 0U; index < values.size(); ++index) {
            points.push_back({ ProfilerFrameX(plot, values.size() - 1U - index), ValueY(plot, values[index], axisMaximum) });
        }
        canvas_.Stroke(points, color, PS_SOLID, 1);
    }

    void PaintThreshold(const RECT& plot, double value, double axisMaximum, EditorColor color, std::string_view label) const {
        if (value <= 0.0) return;
        const int y = ValueY(plot, value, axisMaximum);
        canvas_.HLine(plot.left, plot.right, y, color, PS_DOT);
        canvas_.TextRight(Rect(plot.right - 130, y - 13, plot.right - 4, y - 1), label, color, 9);
    }

    void PaintCursor(const RECT& plot) const {
        const std::optional<std::size_t> offset = ShownOffset();
        if (!offset || profiler_.Panel().selectedFrame == std::nullopt) return;
        const int x = ProfilerFrameX(plot, *offset);
        canvas_.Fill(Rect(x, plot.top, x + 1, plot.bottom), palette::kBudget);
    }

    void PaintUnavailable(const RECT& plot, std::string_view why) const {
        canvas_.Text(plot, why, theme_.textDisabled, 11, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    void PaintChartLabel(const ProfilerChartRow& chart, EditorColor swatch, std::string_view name, std::string_view big, std::string_view detail) const {
        const Canvas& c = canvas_;
        c.Fill(chart.label, theme_.strip);
        c.Fill(Rect(chart.label.right - 1, chart.label.top, chart.label.right, chart.label.bottom), theme_.borderChrome);
        c.Fill(Rect(chart.label.left + kPad, chart.label.top + 9, chart.label.left + kPad + 8, chart.label.top + 17), swatch);
        c.Text(Rect(chart.label.left + kPad + 14, chart.label.top + 4, chart.label.right - kPad, chart.label.top + 22), name, theme_.textPrimary, 12, FW_SEMIBOLD);
        if (!big.empty()) c.Text(Rect(chart.label.left + kPad, chart.label.top + 20, chart.label.right - kPad, chart.label.top + 40), big, theme_.textPrimary, 14, FW_SEMIBOLD);
        if (!detail.empty()) c.Text(Rect(chart.label.left + kPad, chart.label.top + 40, chart.label.right - kPad, chart.label.bottom - 1), detail, theme_.textSecondary, 10);
    }

    // A legend line in the label of a chart: a swatch, a name and a reading at the right.
    void PaintLegend(const RECT& label, int line, EditorColor swatch, std::string_view name, std::string_view value) const {
        const int top = label.top + 24 + line * 16;
        canvas_.Fill(Rect(label.left + kPad, top + 5, label.left + kPad + 8, top + 13), swatch);
        canvas_.Text(Rect(label.left + kPad + 14, top, label.right - 52, top + 18), name, theme_.textSecondary, 10);
        canvas_.TextRight(Rect(label.right - 52, top, label.right - kPad, top + 18), value, theme_.textPrimary, 10);
    }

    void PaintFrameChart(const ProfilerChartRow& chart) const {
        const Canvas& c = canvas_;
        const std::size_t id = ProfilerRegistry::Id(ProfilerBuiltIn::FrameTime);
        const kb::core::ProfilerCounterInfo& info = registry_.Info(id);
        const std::vector<double> values = Series(registry_, ProfilerBuiltIn::FrameTime);
        const kb::core::ProfilerCounterStats stats = registry_.Stats(id);
        const std::string fps = ValueText(registry_, ProfilerRegistry::Id(ProfilerBuiltIn::FramesPerSecond));
        PaintChartLabel(chart, theme_.textSecondary, ProfilerText::Counter(info), stats.available ? FormatProfilerValue(info, stats.now) : "-",
            EditorLocalization::Format("profiler.panel.fps_target", "{fps} FPS, target {target} ms", { { "fps", fps }, { "target", std::format("{:g}", info.budget) } }));
        c.Fill(chart.plot, theme_.panel);
        if (values.empty()) { PaintUnavailable(chart.plot, std::string{ EditorText("profiler.unavailable", "unavailable") }); return; }
        const double axis = AxisMaximum({ values }, info.budget, info.alarm);
        const double step = static_cast<double>(chart.plot.right - chart.plot.left) / static_cast<double>(kHistory - 1U);
        const int barWidth = std::max(1, static_cast<int>(std::ceil(step)) - 1);
        for (std::size_t index = 0U; index < values.size(); ++index) {
            const int x = ProfilerFrameX(chart.plot, values.size() - 1U - index);
            const int y = ValueY(chart.plot, values[index], axis);
            const ProfilerSeverity severity = ProfilerRegistry::Severity(info, values[index]);
            c.Fill(Rect(x - barWidth, y, x, chart.plot.bottom), severity == ProfilerSeverity::Alarm ? palette::kAlarm : severity == ProfilerSeverity::Warning ? palette::kWarning : palette::kBlue);
        }
        PaintThreshold(chart.plot, info.budget, axis, palette::kWarning, std::string{ EditorText("profiler.panel.budget", "budget") } + " " + std::format("{:g}", info.budget));
        PaintThreshold(chart.plot, info.alarm, axis, palette::kAlarm, std::format("{:g}", info.alarm) + " " + std::string{ EditorText("profiler.panel.alarm", "alarm") });
        PaintCursor(chart.plot);
    }

    void PaintCpuChart(const ProfilerChartRow& chart) const {
        const Canvas& c = canvas_;
        const std::vector<double> game = Series(registry_, ProfilerBuiltIn::GameThreadTime);
        const std::vector<double> render = Series(registry_, ProfilerBuiltIn::RenderThreadTime);
        PaintChartLabel(chart, palette::kBlue, std::string{ EditorText("profiler.area.cpu", "CPU") }, "", "");
        PaintLegend(chart.label, 0, palette::kBlue, ProfilerText::Counter(registry_.Info(ProfilerRegistry::Id(ProfilerBuiltIn::GameThreadTime))), ValueText(registry_, ProfilerRegistry::Id(ProfilerBuiltIn::GameThreadTime)));
        PaintLegend(chart.label, 1, palette::kGreen, ProfilerText::Counter(registry_.Info(ProfilerRegistry::Id(ProfilerBuiltIn::RenderThreadTime))), ValueText(registry_, ProfilerRegistry::Id(ProfilerBuiltIn::RenderThreadTime)));
        c.Fill(chart.plot, theme_.panel);
        if (game.empty() && render.empty()) {
            PaintUnavailable(chart.plot, std::string{ EditorText("profiler.panel.game_thread_play", "The game thread is measured in Play Mode") });
            return;
        }
        const kb::core::ProfilerCounterInfo& info = registry_.Info(ProfilerRegistry::Id(ProfilerBuiltIn::FrameTime));
        const double axis = AxisMaximum({ game, render }, info.budget, 0.0);
        PaintThreshold(chart.plot, info.budget, axis, palette::kWarning, std::format("{:g}", info.budget));
        PaintLine(chart.plot, game, axis, palette::kBlue);
        PaintLine(chart.plot, render, axis, palette::kGreen);
        PaintCursor(chart.plot);
    }

    void PaintGpuChart(const ProfilerChartRow& chart) const {
        const std::size_t id = ProfilerRegistry::Id(ProfilerBuiltIn::GpuTime);
        const std::vector<double> values = Series(registry_, ProfilerBuiltIn::GpuTime);
        PaintChartLabel(chart, palette::kWarning, ProfilerText::Counter(registry_.Info(id)), ValueText(registry_, id), "");
        canvas_.Fill(chart.plot, theme_.panel);
        if (values.empty()) {
            PaintUnavailable(chart.plot, std::string{ EditorText("profiler.panel.gpu_timer", "No GPU timer on this device") });
            return;
        }
        const kb::core::ProfilerCounterInfo& info = registry_.Info(id);
        const double axis = AxisMaximum({ values }, info.budget, 0.0);
        PaintThreshold(chart.plot, info.budget, axis, palette::kWarning, std::format("{:g}", info.budget));
        PaintLine(chart.plot, values, axis, palette::kWarning);
        PaintCursor(chart.plot);
    }

    void PaintMemoryChart(const ProfilerChartRow& chart) const {
        const std::size_t id = ProfilerRegistry::Id(ProfilerBuiltIn::ProcessMemory);
        const std::vector<double> values = Series(registry_, ProfilerBuiltIn::ProcessMemory);
        const std::size_t vram = ProfilerRegistry::Id(ProfilerBuiltIn::GpuMemory);
        PaintChartLabel(chart, palette::kPurple, std::string{ EditorText("profiler.group.memory", "Memory") }, ValueText(registry_, id),
            registry_.Stats(vram).available ? "VRAM " + ValueText(registry_, vram) : std::string{});
        canvas_.Fill(chart.plot, theme_.panel);
        if (values.empty()) { PaintUnavailable(chart.plot, std::string{ EditorText("profiler.unavailable", "unavailable") }); return; }
        const double low = *std::ranges::min_element(values);
        const double high = *std::ranges::max_element(values);
        // The axis starts at the lowest reading, so a memory that creeps up shows as a slope.
        const double span = std::max(high - low, std::max(1.0, high * 0.02));
        std::vector<POINT> points;
        for (std::size_t index = 0U; index < values.size(); ++index) {
            const int y = chart.plot.bottom - 3 - static_cast<int>((values[index] - low) / span * (chart.plot.bottom - chart.plot.top - 8));
            points.push_back({ ProfilerFrameX(chart.plot, values.size() - 1U - index), y });
        }
        std::vector<POINT> area = points;
        area.push_back({ points.back().x, chart.plot.bottom });
        area.push_back({ points.front().x, chart.plot.bottom });
        HBRUSH brush = CreateSolidBrush(Canvas::C(canvas_.Mix(theme_.panel, palette::kPurple, 22)));
        HPEN none = CreatePen(PS_NULL, 0, 0);
        HGDIOBJ previousBrush = SelectObject(canvas_.dc, brush);
        HGDIOBJ previousPen = SelectObject(canvas_.dc, none);
        Polygon(canvas_.dc, area.data(), static_cast<int>(area.size()));
        SelectObject(canvas_.dc, previousPen);
        SelectObject(canvas_.dc, previousBrush);
        DeleteObject(none);
        DeleteObject(brush);
        canvas_.Stroke(points, palette::kPurple);
        PaintCursor(chart.plot);
    }

    void PaintDrawingChart(const ProfilerChartRow& chart) const {
        const std::size_t draws = ProfilerRegistry::Id(ProfilerBuiltIn::DrawCalls);
        const std::size_t triangles = ProfilerRegistry::Id(ProfilerBuiltIn::Triangles);
        PaintChartLabel(chart, palette::kCyan, std::string{ EditorText("profiler.group.drawing", "Drawing") }, "", "");
        PaintLegend(chart.label, 0, palette::kBlue, std::string{ EditorText("profiler.panel.draws", "Draw calls") }, ValueText(registry_, draws));
        PaintLegend(chart.label, 1, palette::kWarning, std::string{ EditorText("profiler.counter.triangles", "Triangles") }, ValueText(registry_, triangles));
        canvas_.Fill(chart.plot, theme_.panel);
        const std::vector<double> drawValues = Series(registry_, ProfilerBuiltIn::DrawCalls);
        const std::vector<double> triangleValues = Series(registry_, ProfilerBuiltIn::Triangles);
        if (drawValues.empty() && triangleValues.empty()) { PaintUnavailable(chart.plot, std::string{ EditorText("profiler.unavailable", "unavailable") }); return; }
        // Each line has its own scale: the point is how it moves, the numbers are in the label.
        PaintLine(chart.plot, drawValues, AxisMaximum({ drawValues }, 0.0, 0.0), palette::kBlue);
        PaintLine(chart.plot, triangleValues, AxisMaximum({ triangleValues }, 0.0, 0.0), palette::kWarning);
        PaintCursor(chart.plot);
    }

    void PaintRuler() const {
        const Canvas& c = canvas_;
        const ProfilerPanelState& state = profiler_.Panel();
        c.Fill(geometry_.ruler, theme_.strip);
        c.Fill(Rect(geometry_.ruler.left, geometry_.ruler.bottom - 1, geometry_.ruler.right, geometry_.ruler.bottom), theme_.borderChrome);
        const std::size_t frames = registry_.Stats(ProfilerRegistry::Id(ProfilerBuiltIn::FrameTime)).samples;
        std::array<double, kHistory> samples{};
        const std::size_t count = registry_.Samples(ProfilerRegistry::Id(ProfilerBuiltIn::FrameTime), samples);
        const double seconds = std::accumulate(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(count), 0.0) / 1000.0;
        const std::string live = state.paused ? std::string{ EditorText("profiler.panel.paused", "Paused") } : state.selectedFrame ? std::string{ EditorText("profiler.panel.frame_picked", "Frame picked") } : std::string{ EditorText("profiler.panel.live", "Live") };
        const RECT pill = Rect(geometry_.ruler.left + kPad, geometry_.ruler.top + 4, geometry_.ruler.left + kPad + 14 + static_cast<int>(live.size()) * 7, geometry_.ruler.bottom - 4);
        c.Pill(pill, live, state.paused || state.selectedFrame ? theme_.textSecondary : palette::kBudget, c.Mix(theme_.panel, state.paused || state.selectedFrame ? theme_.textSecondary : palette::kBudget, 18));
        c.Text(Rect(pill.right + 12, geometry_.ruler.top, geometry_.ruler.right - 260, geometry_.ruler.bottom),
            EditorLocalization::Format("profiler.panel.last_frames", "last {frames} frames ({seconds} s)", { { "frames", std::to_string(frames) }, { "seconds", std::format("{:.1f}", seconds) } }),
            theme_.textSecondary, 10);
        const std::optional<std::uint64_t> shown = ProfilerShownFrame(profiler_);
        c.TextRight(Rect(geometry_.ruler.right - 260, geometry_.ruler.top, geometry_.ruler.right - kPad, geometry_.ruler.bottom),
            shown && state.selectedFrame ? EditorLocalization::Format("profiler.panel.selected", "selected: #{frame}", { { "frame", std::to_string(*shown) } })
                                          : std::string{ EditorText("profiler.panel.pick_hint", "Click a chart to pick a frame") },
            shown && state.selectedFrame ? palette::kBudget : theme_.textDisabled, 10);
    }

    // The hierarchy of regions of the frame being shown.
    void PaintHierarchy() const {
        const Canvas& c = canvas_;
        const ProfilerPanelState& state = profiler_.Panel();
        c.Fill(geometry_.hierarchyTabs, theme_.strip);
        c.Fill(Rect(geometry_.hierarchyTabs.left + 8, geometry_.hierarchyTabs.top + 3, geometry_.hierarchyTabs.left + 8 + 100, geometry_.hierarchyTabs.bottom - 3), theme_.tabActive);
        c.Text(Rect(geometry_.hierarchyTabs.left + 8, geometry_.hierarchyTabs.top, geometry_.hierarchyTabs.left + 108, geometry_.hierarchyTabs.bottom), std::string{ EditorText("profiler.panel.hierarchy", "Hierarchy") }, theme_.textPrimary, 11, FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        const std::optional<std::uint64_t> shown = ProfilerShownFrame(profiler_);
        const kb::core::ProfilerFrameRecord* record = shown ? profiler_.FrameRegions().Find(*shown) : nullptr;
        if (record != nullptr) {
            c.TextRight(Rect(geometry_.hierarchyTabs.right - 300, geometry_.hierarchyTabs.top, geometry_.hierarchyTabs.right - kPad, geometry_.hierarchyTabs.bottom),
                EditorLocalization::Format("profiler.panel.frame_info", "Frame #{frame} - {ms} ms", { { "frame", std::to_string(record->frame) },
                    { "ms", std::format("{:.2f}", static_cast<double>(record->FrameNanoseconds()) / 1e6) } }), theme_.textSecondary, 10);
        }
        // Column header: name, share of the frame, time in all, time of its own, calls.
        const RECT header = geometry_.hierarchyHeader;
        c.Fill(header, theme_.strip);
        c.Fill(Rect(header.left, header.bottom - 1, header.right, header.bottom), theme_.borderChrome);
        const int calls = header.right - kPad - 70;
        const int own = calls - 84;
        const int total = own - 84;
        const int bar = total - 150;
        c.Text(Rect(header.left + kPad, header.top, bar, header.bottom), std::string{ EditorText("profiler.table.name", "Name") }, theme_.textDisabled, 10);
        c.Text(Rect(bar + 6, header.top, total, header.bottom), std::string{ EditorText("profiler.table.share", "Share") }, theme_.textDisabled, 10);
        c.TextRight(Rect(total, header.top, own - 6, header.bottom), std::string{ EditorText("profiler.table.total_ms", "Total ms") }, theme_.textDisabled, 10);
        c.TextRight(Rect(own, header.top, calls - 6, header.bottom), std::string{ EditorText("profiler.table.self_ms", "Own ms") }, theme_.textDisabled, 10);
        c.TextRight(Rect(calls, header.top, header.right - kPad, header.bottom), std::string{ EditorText("profiler.table.calls", "Calls") }, theme_.textDisabled, 10);

        const RECT list = geometry_.hierarchyRows;
        if (record == nullptr || record->rows.empty()) {
            c.Text(list, std::string{ EditorText("profiler.panel.no_regions", "No regions: measure the CPU area and draw the scene.") }, theme_.textDisabled, 11, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return;
        }
        std::vector<std::string> hot;
        for (const kb::core::DiagnosisFinding& finding : profiler_.Findings()) {
            if (finding.rule == "hot_region") hot.push_back(finding.subject);
        }
        const int saved = SaveDC(c.dc);
        Clip(list);
        const double frameNanoseconds = static_cast<double>(std::max<std::uint64_t>(1U, record->FrameNanoseconds()));
        int top = list.top - state.hierarchyScroll;
        int stripe = 0;
        for (const kb::core::ProfilerRegionRow& row : record->rows) {
            const RECT r = Rect(list.left, top, list.right, top + ProfilerPanelGeometry::kRowHeight);
            if (r.bottom > list.top && r.top < list.bottom) {
                if (stripe % 2 == 0) c.Fill(r, c.Mix(theme_.panel, theme_.strip, 50));
                const double share = static_cast<double>(row.totalNanoseconds) / frameNanoseconds;
                const bool flagged = std::ranges::find(hot, row.name) != hot.end() && row.depth >= 1U;
                const int indent = kPad + 14 * static_cast<int>(row.depth);
                if (flagged) c.Fill(Rect(r.left + 2, r.top + 6, r.left + 8, r.top + 16), palette::kWarning);
                std::string name{ ProfilerText::Region(row.name) };
                if (row.calls > 1U) name += std::format("  (x{})", row.calls);
                c.Text(Rect(r.left + indent, r.top, bar - 4, r.bottom), name, flagged ? palette::kWarning : row.waiting ? theme_.textDisabled : theme_.textPrimary);
                c.Fill(Rect(bar + 6, r.top + 4, bar + 6 + static_cast<int>(share * 100.0), r.bottom - 4), c.Mix(theme_.panel, palette::kBlue, 30));
                c.Text(Rect(bar + 12, r.top, total, r.bottom), std::format("{:.1f}%", share * 100.0), theme_.textPrimary, 10);
                c.TextRight(Rect(total, r.top, own - 6, r.bottom), std::format("{:.2f}", static_cast<double>(row.totalNanoseconds) / 1e6), theme_.textPrimary);
                c.TextRight(Rect(own, r.top, calls - 6, r.bottom), std::format("{:.2f}", static_cast<double>(row.selfNanoseconds) / 1e6), theme_.textSecondary);
                c.TextRight(Rect(calls, r.top, header.right - kPad, r.bottom), std::to_string(row.calls), theme_.textSecondary);
            }
            top += ProfilerPanelGeometry::kRowHeight;
            ++stripe;
        }
        RestoreDC(c.dc, saved);
    }

    void PaintMeasurements() const {
        PaintRuler();
        for (const ProfilerChartRow& chart : geometry_.charts) {
            canvas_.Fill(Rect(chart.row.left, chart.row.bottom - 1, chart.row.right, chart.row.bottom), theme_.borderChrome);
            switch (chart.kind) {
            case ProfilerChartKind::Frame: PaintFrameChart(chart); break;
            case ProfilerChartKind::Cpu: PaintCpuChart(chart); break;
            case ProfilerChartKind::Gpu: PaintGpuChart(chart); break;
            case ProfilerChartKind::Memory: PaintMemoryChart(chart); break;
            case ProfilerChartKind::Drawing: PaintDrawingChart(chart); break;
            }
            canvas_.Fill(Rect(chart.row.left, chart.row.bottom - 1, chart.row.right, chart.row.bottom), theme_.borderChrome);
        }
        PaintHierarchy();
    }

    void PaintCounters() const {
        const Canvas& c = canvas_;
        // Summary: how many counters are over their alarm, over their budget, and fine.
        int alarm = 0;
        int budget = 0;
        int normal = 0;
        for (std::size_t counter = 0U; counter < registry_.CounterCount(); ++counter) {
            const kb::core::ProfilerCounterStats stats = registry_.Stats(counter);
            if (!stats.available || !selection_.AreaMeasured(registry_.Info(counter).area)) continue;
            const ProfilerSeverity severity = ProfilerRegistry::Severity(registry_.Info(counter), stats.now);
            (severity == ProfilerSeverity::Alarm ? alarm : severity == ProfilerSeverity::Warning ? budget : normal)++;
        }
        c.Fill(geometry_.summary, theme_.strip);
        c.Fill(Rect(geometry_.summary.left, geometry_.summary.bottom - 1, geometry_.summary.right, geometry_.summary.bottom), theme_.borderChrome);
        int x = geometry_.summary.left + 12;
        const int top = geometry_.summary.top + 7;
        const auto pill = [&](int count, std::string text, EditorColor color) {
            const int width = 24 + static_cast<int>(text.size()) * 6;
            c.Pill(Rect(x, top, x + width, top + 20), text, color, c.Mix(theme_.panel, color, 22));
            x += width + 8;
            static_cast<void>(count);
        };
        pill(alarm, EditorLocalization::Format("profiler.panel.pill_alarm", "{count} over alarm", { { "count", std::to_string(alarm) } }), palette::kAlarm);
        pill(budget, EditorLocalization::Format("profiler.panel.pill_budget", "{count} over budget", { { "count", std::to_string(budget) } }), palette::kWarning);
        pill(normal, EditorLocalization::Format("profiler.panel.pill_normal", "{count} fine", { { "count", std::to_string(normal) } }), palette::kGreen);
        c.TextRight(Rect(x, geometry_.summary.top, geometry_.summary.right - 12, geometry_.summary.bottom),
            std::string{ EditorText("profiler.panel.thresholds_hint", "Click a budget or an alarm to change it; the same line is drawn on the charts and the scene.") }, theme_.textSecondary, 10);

        const ProfilerCounterColumns columns = ResolveProfilerCounterColumns(geometry_.tableRows);
        const RECT header = geometry_.tableHeader;
        c.Fill(header, theme_.strip);
        c.Fill(Rect(header.left, header.bottom - 1, header.right, header.bottom), theme_.borderChrome);
        c.Text(Rect(columns.name, header.top, columns.now, header.bottom), std::string{ EditorText("profiler.counters.caption", "Counter") }, theme_.textDisabled, 10);
        const std::array<std::pair<int, std::string>, 7> numeric{ { { columns.now, std::string{ EditorText("profiler.table.now", "Now") } }, { columns.average, std::string{ EditorText("profiler.table.average", "Average") } },
            { columns.minimum, std::string{ EditorText("profiler.table.min", "Min") } }, { columns.maximum, std::string{ EditorText("profiler.table.max", "Max") } }, { columns.p95, "p95" },
            { columns.budget, std::string{ EditorText("profiler.panel.budget_label", "Budget") } }, { columns.alarm, std::string{ EditorText("profiler.panel.alarm_label", "Alarm") } } } };
        const std::array<int, 7> rights{ columns.average, columns.minimum, columns.maximum, columns.p95, columns.budget, columns.alarm, columns.spark };
        for (std::size_t index = 0U; index < numeric.size(); ++index) {
            c.TextRight(Rect(numeric[index].first, header.top, rights[index] - 6, header.bottom), numeric[index].second, theme_.textDisabled, 10);
        }
        c.Text(Rect(columns.spark + 14, header.top, columns.scene, header.bottom), std::string{ EditorText("profiler.table.history", "Last frames") }, theme_.textDisabled, 10);
        c.Text(Rect(columns.scene, header.top, columns.end, header.bottom), std::string{ EditorText("profiler.column.scene", "on scene") }, theme_.textDisabled, 10, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        const RECT list = geometry_.tableRows;
        const int saved = SaveDC(c.dc);
        Clip(list);
        int y = list.top - profiler_.Panel().countersScroll;
        for (const ProfilerCounterRow& row : ProfilerCounterRows(registry_)) {
            const kb::core::ProfilerCounterInfo& info = registry_.Info(row.counter);
            if (row.startsGroup) {
                PaintGroupHeader(Rect(list.left, y, list.right, y + ProfilerPanelGeometry::kRowHeight), ProfilerText::Group(info.group));
                y += ProfilerPanelGeometry::kRowHeight;
            }
            const RECT r = Rect(list.left, y, list.right, y + ProfilerPanelGeometry::kCounterRowHeight);
            if (r.bottom > list.top && r.top < list.bottom) PaintCounterRow(r, row.counter, columns);
            y += ProfilerPanelGeometry::kCounterRowHeight;
        }
        RestoreDC(c.dc, saved);
    }

    void PaintCounterRow(const RECT& r, std::size_t counter, const ProfilerCounterColumns& columns) const {
        const Canvas& c = canvas_;
        const kb::core::ProfilerCounterInfo& info = registry_.Info(counter);
        const kb::core::ProfilerCounterStats stats = registry_.Stats(counter);
        const bool measured = selection_.AreaMeasured(info.area);
        c.Fill(Rect(r.left, r.bottom - 1, r.right, r.bottom), theme_.borderChrome);
        EditorDialogStyle::PaintCheckbox(c.dc, Rect(columns.check, r.top + 5, columns.check + 14, r.top + 19), theme_, selection_.InPanel(info.id), measured);
        const EditorColor ink = measured ? theme_.textPrimary : theme_.textDisabled;
        c.Text(Rect(columns.name, r.top, columns.now - 4, r.bottom), ProfilerText::Counter(info), ink);
        const Reading reading = measured ? ReadingOf(registry_, counter, theme_) : Reading{ false, "-", theme_.textDisabled };
        c.TextRight(Rect(columns.now, r.top, columns.average - 6, r.bottom), reading.text, reading.color, 11, FW_SEMIBOLD);
        const bool shown = measured && stats.available;
        const std::array<std::pair<int, double>, 4> numbers{ { { columns.average, stats.average }, { columns.minimum, stats.minimum }, { columns.maximum, stats.maximum }, { columns.p95, stats.p95 } } };
        const std::array<int, 4> rights{ columns.minimum, columns.maximum, columns.p95, columns.budget };
        for (std::size_t index = 0U; index < numbers.size(); ++index) {
            c.TextRight(Rect(numbers[index].first, r.top, rights[index] - 6, r.bottom), shown ? FormatProfilerValue(info, numbers[index].second) : "-", theme_.textSecondary);
        }
        c.TextRight(Rect(columns.budget, r.top, columns.alarm - 6, r.bottom), ThresholdText(info, info.budget, false), palette::kWarning);
        c.TextRight(Rect(columns.alarm, r.top, columns.spark - 6, r.bottom), ThresholdText(info, info.alarm, true), palette::kAlarm);
        if (shown) {
            std::array<double, kHistory> samples{};
            const std::size_t count = registry_.Samples(counter, samples);
            const double low = *std::min_element(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(count));
            const double high = *std::max_element(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(count));
            const double span = std::max(high - low, 1e-9);
            const RECT spark = Rect(columns.spark + 14, r.top + 4, columns.scene - 6, r.bottom - 4);
            std::vector<POINT> points;
            for (std::size_t index = 0U; index < count; ++index) {
                const int x = spark.right - static_cast<int>((count - 1U - index) * static_cast<std::size_t>(spark.right - spark.left) / kHistory);
                points.push_back({ x, spark.bottom - static_cast<int>((samples[index] - low) / span * (spark.bottom - spark.top)) });
            }
            c.Stroke(points, AreaColor(info));
        }
        EditorDialogStyle::PaintCheckbox(c.dc, Rect((columns.scene + columns.end) / 2 - 7, r.top + 5, (columns.scene + columns.end) / 2 + 7, r.top + 19), theme_, selection_.OnScene(info.id), measured);
    }

    // Diagnosis.
    void PaintDiagnosisLeft() const {
        const Canvas& c = canvas_;
        c.Fill(geometry_.left, theme_.strip);
        c.Fill(Rect(geometry_.left.right - 1, geometry_.left.top, geometry_.left.right, geometry_.left.bottom), theme_.borderChrome);
        const ProfilerPanelState& state = profiler_.Panel();
        std::array<int, 3> severityCount{};
        std::array<int, 4> categoryCount{};
        for (const kb::core::DiagnosisFinding& finding : profiler_.Findings()) {
            ++severityCount[static_cast<std::size_t>(finding.severity)];
            ++categoryCount[static_cast<std::size_t>(finding.category)];
        }
        int y = geometry_.left.top;
        const auto header = [&](std::string_view text) {
            PaintSectionHeader(Rect(geometry_.left.left, y, geometry_.left.right - 1, y + 28), text);
            y += 28;
        };
        const auto row = [&](bool checked, std::string_view text, int count, std::optional<EditorColor> swatch) {
            const RECT r = Rect(geometry_.left.left, y, geometry_.left.right - 1, y + ProfilerPanelGeometry::kRowHeight);
            EditorDialogStyle::PaintCheckbox(c.dc, Rect(r.left + kPad, r.top + 4, r.left + kPad + 14, r.top + 18), theme_, checked);
            int textLeft = r.left + kPad + 24;
            if (swatch) {
                c.Fill(Rect(textLeft, r.top + 7, textLeft + 8, r.top + 15), *swatch);
                textLeft += 14;
            }
            c.Text(Rect(textLeft, r.top, r.right - 40, r.bottom), text, checked ? theme_.textPrimary : theme_.textSecondary);
            c.TextRight(Rect(r.right - 38, r.top, r.right - kPad, r.bottom), std::to_string(count), theme_.textDisabled, 10);
            y += ProfilerPanelGeometry::kRowHeight;
        };
        header(std::string{ EditorText("profiler.diagnosis.severity", "Severity") });
        const std::array<EditorColor, 3> severityColors{ palette::kAlarm, palette::kWarning, palette::kBlue };
        for (std::size_t index = 0U; index < 3U; ++index) {
            row(state.showSeverity[index], ProfilerText::Severity(static_cast<kb::core::DiagnosisSeverity>(index)), severityCount[index], severityColors[index]);
        }
        header(std::string{ EditorText("profiler.diagnosis.categories", "Categories") });
        const std::array<EditorColor, 4> categoryColors{ palette::kBlue, palette::kWarning, palette::kPurple, theme_.textSecondary };
        for (std::size_t index = 0U; index < 4U; ++index) {
            row(state.showCategory[index], ProfilerText::Category(static_cast<kb::core::DiagnosisCategory>(index)), categoryCount[index], categoryColors[index]);
        }
        header(std::string{ EditorText("profiler.diagnosis.ignored", "Ignored") });
        row(state.showIgnored, std::string{ EditorText("profiler.diagnosis.show_ignored", "Show ignored") }, static_cast<int>(state.ignored.size()), std::nullopt);
        c.Text(Rect(geometry_.left.left + kPad, y + 8, geometry_.left.right - kPad, geometry_.left.bottom - 8),
            std::string{ EditorText("profiler.diagnosis.rules_note", "The diagnosis compares the measurements with the budgets of the counters. Change a budget in Counters and the findings follow.") },
            theme_.textDisabled, 10, FW_NORMAL, DT_LEFT | DT_TOP | DT_WORDBREAK);
    }

    [[nodiscard]] static EditorColor SeverityTone(kb::core::DiagnosisSeverity severity) {
        return severity == kb::core::DiagnosisSeverity::Problem ? palette::kAlarm : severity == kb::core::DiagnosisSeverity::Warning ? palette::kWarning : palette::kBlue;
    }

    void PaintDiagnosis() const {
        const Canvas& c = canvas_;
        const std::vector<const kb::core::DiagnosisFinding*> findings = ProfilerVisibleFindings(profiler_);
        const ProfilerPanelState& state = profiler_.Panel();
        c.Fill(geometry_.summary, theme_.strip);
        c.Fill(Rect(geometry_.summary.left, geometry_.summary.bottom - 1, geometry_.summary.right, geometry_.summary.bottom), theme_.borderChrome);
        // The time to win back: what it takes to reach the budget when the frame is over it, else
        // the gains of the findings, which cannot be more than the frame.
        const kb::core::ProfilerCounterStats frame = registry_.Stats(ProfilerRegistry::Id(ProfilerBuiltIn::FrameTime));
        double gain = 0.0;
        bool overBudget = false;
        for (const kb::core::DiagnosisFinding& finding : profiler_.Findings()) {
            if (state.ignored.contains(finding.rule + "/" + finding.subject)) continue;
            if (finding.rule == "frame_over_budget") { gain = std::max(0.0, finding.gainMilliseconds); overBudget = true; }
            else if (!overBudget) gain += std::max(0.0, finding.gainMilliseconds);
        }
        if (frame.available) gain = std::min(gain, frame.average);
        c.Text(Rect(geometry_.summary.left + 12, geometry_.summary.top + 4, geometry_.summary.left + 130, geometry_.summary.top + 34),
            gain > 0.0 ? "-" + std::format("{:.1f} ms", gain) : std::string{ EditorText("profiler.diagnosis.nothing", "Nothing to fix") }, gain > 0.0 ? palette::kBlue : palette::kGreen, 18, FW_SEMIBOLD);
        c.Text(Rect(geometry_.summary.left + 140, geometry_.summary.top + 4, geometry_.summary.right - 12, geometry_.summary.top + 34),
            gain > 0.0 && frame.available && frame.average > 0.0
                ? EditorLocalization::Format("profiler.diagnosis.recoverable", "to win back ({percent}% of the frame) by fixing what was measured", { { "percent", std::format("{:.0f}", 100.0 * gain / frame.average) } })
                : std::string{ EditorText("profiler.diagnosis.from_measurements", "Findings come from the measurements of the last frames.") },
            theme_.textSecondary, 11);
        c.Text(Rect(geometry_.summary.left + 12, geometry_.summary.top + 34, geometry_.summary.right - 12, geometry_.summary.bottom - 2),
            EditorLocalization::Format("profiler.diagnosis.listed", "{count} findings listed", { { "count", std::to_string(findings.size()) } }), theme_.textDisabled, 10);

        const RECT header = geometry_.tableHeader;
        c.Fill(header, theme_.strip);
        c.Fill(Rect(header.left, header.bottom - 1, header.right, header.bottom), theme_.borderChrome);
        const int gainLeft = header.right - kPad - 70;
        const int whereLeft = gainLeft - 200;
        c.Text(Rect(header.left + kPad, header.top, header.left + kPad + 80, header.bottom), std::string{ EditorText("profiler.diagnosis.col_severity", "Severity") }, theme_.textDisabled, 10);
        c.Text(Rect(header.left + kPad + 92, header.top, whereLeft, header.bottom), std::string{ EditorText("profiler.diagnosis.col_what", "What is wrong") }, theme_.textDisabled, 10);
        c.Text(Rect(whereLeft, header.top, gainLeft, header.bottom), std::string{ EditorText("profiler.diagnosis.col_where", "Where") }, theme_.textDisabled, 10);
        c.TextRight(Rect(gainLeft, header.top, header.right - kPad, header.bottom), std::string{ EditorText("profiler.diagnosis.col_gain", "Gain") }, theme_.textDisabled, 10);

        const RECT list = geometry_.tableRows;
        if (findings.empty()) {
            c.Text(list, std::string{ EditorText("profiler.diagnosis.empty", "No findings: the measured frames are inside their budgets.") }, theme_.textDisabled, 11, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return;
        }
        const int saved = SaveDC(c.dc);
        Clip(list);
        for (std::size_t index = 0U; index < findings.size(); ++index) {
            const kb::core::DiagnosisFinding& finding = *findings[index];
            const RECT r = Rect(list.left, list.top + static_cast<int>(index) * kFindingHeight - state.findingsScroll, list.right,
                list.top + static_cast<int>(index + 1U) * kFindingHeight - state.findingsScroll);
            if (r.bottom <= list.top || r.top >= list.bottom) continue;
            const bool ignored = state.ignored.contains(finding.rule + "/" + finding.subject);
            if (static_cast<int>(index) == state.selectedFinding) c.Fill(r, theme_.tabActive);
            c.Fill(Rect(r.left, r.bottom - 1, r.right, r.bottom), theme_.borderChrome);
            const EditorColor tone = SeverityTone(finding.severity);
            c.Pill(Rect(r.left + kPad, r.top + 14, r.left + kPad + 80, r.top + 34), ProfilerText::Severity(finding.severity), tone, c.Mix(theme_.panel, tone, 22));
            c.Text(Rect(r.left + kPad + 92, r.top + 5, whereLeft - 8, r.top + 25), ProfilerText::FindingTitle(finding, registry_), ignored ? theme_.textDisabled : theme_.textPrimary, 12, FW_SEMIBOLD);
            c.Text(Rect(r.left + kPad + 92, r.top + 25, whereLeft - 8, r.bottom - 3), ProfilerText::FindingDetail(finding), theme_.textSecondary, 10);
            c.Text(Rect(whereLeft, r.top, gainLeft, r.bottom), ProfilerText::FindingWhere(finding, registry_), theme_.textSecondary);
            c.TextRight(Rect(gainLeft, r.top, r.right - kPad, r.bottom), finding.gainMilliseconds > 0.0 ? "-" + std::format("{:.1f} ms", finding.gainMilliseconds) : "-", finding.gainMilliseconds > 0.0 ? palette::kBlue : theme_.textDisabled, 12, FW_SEMIBOLD);
        }
        RestoreDC(c.dc, saved);
    }

    void PaintDiagnosisDetails() const {
        const Canvas& c = canvas_;
        const RECT r = geometry_.right;
        c.Fill(r, theme_.strip);
        c.Fill(Rect(r.left, r.top, r.left + 1, r.bottom), theme_.borderChrome);
        const std::vector<const kb::core::DiagnosisFinding*> findings = ProfilerVisibleFindings(profiler_);
        const int selected = profiler_.Panel().selectedFinding;
        PaintSectionHeader(Rect(r.left + 1, r.top, r.right, r.top + 28), std::string{ EditorText("profiler.diagnosis.details", "Details") });
        if (selected < 0 || selected >= static_cast<int>(findings.size())) {
            c.Text(Rect(r.left + 12, r.top + 40, r.right - 12, r.top + 100), std::string{ EditorText("profiler.diagnosis.pick", "Pick a finding to see why it hurts and what to do.") }, theme_.textDisabled, 11, FW_NORMAL, DT_LEFT | DT_TOP | DT_WORDBREAK);
            return;
        }
        const kb::core::DiagnosisFinding& finding = *findings[static_cast<std::size_t>(selected)];
        const EditorColor tone = SeverityTone(finding.severity);
        int y = r.top + 38;
        c.Pill(Rect(r.left + 12, y, r.left + 12 + 80, y + 20), ProfilerText::Severity(finding.severity), tone, c.Mix(theme_.panel, tone, 22));
        c.Pill(Rect(r.left + 100, y, r.left + 100 + 60, y + 20), ProfilerText::Category(finding.category), theme_.textSecondary, c.Mix(theme_.panel, theme_.textSecondary, 18));
        c.TextRight(Rect(r.right - 110, y, r.right - 12, y + 20), finding.gainMilliseconds > 0.0 ? "-" + std::format("{:.1f} ms", finding.gainMilliseconds) : "", palette::kBlue, 14, FW_SEMIBOLD);
        y += 28;
        c.Text(Rect(r.left + 12, y, r.right - 12, y + 40), ProfilerText::FindingTitle(finding, registry_), theme_.textPrimary, 13, FW_SEMIBOLD, DT_LEFT | DT_TOP | DT_WORDBREAK);
        y += 48;
        PaintSectionHeader(Rect(r.left + 1, y, r.right, y + 26), std::string{ EditorText("profiler.diagnosis.why", "Why it hurts") });
        y += 30;
        c.Text(Rect(r.left + 12, y, r.right - 12, y + 70), ProfilerText::FindingDetail(finding), theme_.textSecondary, 11, FW_NORMAL, DT_LEFT | DT_TOP | DT_WORDBREAK);
        y += 76;
        PaintSectionHeader(Rect(r.left + 1, y, r.right, y + 26), std::string{ EditorText("profiler.diagnosis.todo", "What to do") });
        y += 30;
        int number = 1;
        for (const std::string& step : ProfilerText::FindingSteps(finding)) {
            c.Pill(Rect(r.left + 12, y + 1, r.left + 30, y + 19), std::to_string(number++), theme_.textPrimary, theme_.accent);
            c.Text(Rect(r.left + 38, y, r.right - 12, y + 44), step, theme_.textSecondary, 11, FW_NORMAL, DT_LEFT | DT_TOP | DT_WORDBREAK);
            y += 46;
        }
        PaintSectionHeader(Rect(r.left + 1, y, r.right, y + 26), std::string{ EditorText("profiler.diagnosis.proof", "Proof from the measurement") });
        y += 32;
        if (finding.limit > 0.0 && finding.rule != "memory_growth") {
            c.Text(Rect(r.left + 12, y, r.right - 90, y + 18), std::string{ EditorText("profiler.diagnosis.measured", "measured") }, theme_.textSecondary, 10);
            c.TextRight(Rect(r.right - 90, y, r.right - 12, y + 18), std::format("{:.1f}", finding.value), theme_.textPrimary, 10);
            y += 20;
            const int barWidth = r.right - r.left - 24;
            const double ratio = std::clamp(finding.value / std::max(finding.value, finding.limit), 0.0, 1.0);
            c.Fill(Rect(r.left + 12, y, r.left + 12 + barWidth, y + 6), theme_.background);
            c.Fill(Rect(r.left + 12, y, r.left + 12 + static_cast<int>(barWidth * ratio), y + 6), tone);
            y += 12;
            c.Text(Rect(r.left + 12, y, r.right - 90, y + 18), std::string{ EditorText("profiler.diagnosis.limit", "limit") }, theme_.textSecondary, 10);
            c.TextRight(Rect(r.right - 90, y, r.right - 12, y + 18), std::format("{:.1f}", finding.limit), theme_.textPrimary, 10);
            y += 20;
            const double limitRatio = std::clamp(finding.limit / std::max(finding.value, finding.limit), 0.0, 1.0);
            c.Fill(Rect(r.left + 12, y, r.left + 12 + barWidth, y + 6), theme_.background);
            c.Fill(Rect(r.left + 12, y, r.left + 12 + static_cast<int>(barWidth * limitRatio), y + 6), palette::kGreen);
        }
        // The buttons are items of the hit list, at the bottom of the pane.
        const int buttonTop = r.bottom - 40;
        EditorDialogStyle::PaintButton(c.dc, Rect(r.left + 12, buttonTop, r.left + 12 + 168, buttonTop + 26), theme_, std::string{ EditorText("profiler.diagnosis.show", "Show in measurements") }, EditorDialogButtonTone::Primary);
        EditorDialogStyle::PaintButton(c.dc, Rect(r.left + 12 + 176, buttonTop, r.right - 12, buttonTop + 26), theme_, std::string{ EditorText("profiler.diagnosis.ignore", "Ignore") });
    }

    void PaintStatus() const {
        const Canvas& c = canvas_;
        c.Fill(geometry_.status, theme_.chrome);
        const kb::core::ProfilerRecorder& recorder = profiler_.Recorder();
        const std::size_t frames = registry_.Stats(ProfilerRegistry::Id(ProfilerBuiltIn::FrameTime)).samples;
        const kb::core::ProfilerCounterInfo& info = registry_.Info(ProfilerRegistry::Id(ProfilerBuiltIn::FrameTime));
        std::string left = EditorLocalization::Format("profiler.panel.buffer", "Buffer: {frames} frames", { { "frames", std::to_string(frames) } });
        if (recorder.Recording()) {
            left += "   " + EditorLocalization::Format("profiler.panel.recording", "Recording: {frames} frames", { { "frames", std::to_string(recorder.Frames()) } });
        }
        c.Text(Rect(geometry_.status.left + 12, geometry_.status.top, geometry_.status.left + 360, geometry_.status.bottom), left, theme_.textSecondary, 10);
        c.Text(Rect(geometry_.status.left + 370, geometry_.status.top, geometry_.status.left + 620, geometry_.status.bottom),
            info.budget > 0.0 ? EditorLocalization::Format("profiler.panel.target", "Target: {fps} FPS ({ms} ms)", { { "fps", std::format("{:.0f}", 1000.0 / info.budget) }, { "ms", std::format("{:g}", info.budget) } }) : std::string{},
            theme_.textSecondary, 10);
        c.TextRight(Rect(geometry_.status.left + 630, geometry_.status.top, geometry_.status.right - 12, geometry_.status.bottom), profiler_.Panel().lastSaved, theme_.textDisabled, 10);
    }
};

} // namespace

void ProfilerPanel::Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    const EditorProfilerState& profiler = sceneContext.Profiler();
    const ProfilerSelection& selection = sceneContext.EditorConfig().profiler;
    const ProfilerPanelGeometry geometry = ResolveProfilerPanelGeometry(content, profiler.Panel(), selection);
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, content.left, content.top, content.right, content.bottom);
    PanelPainter{ dc, theme, profiler, selection, geometry }.Paint();
    RestoreDC(dc, saved);
}

} // namespace kb::editor

#endif
