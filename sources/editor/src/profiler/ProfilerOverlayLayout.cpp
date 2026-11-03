#include "profiler/ProfilerOverlayLayout.hpp"

#include "localization/EditorLocalization.hpp"
#include "profiler/ProfilerPalette.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>

namespace kb::editor {
namespace {

using Label = EditorSceneViewportTextLabel;
using Shape = EditorSceneViewportOverlayShape;
using Align = EditorSceneViewportTextAlign;

constexpr float kMargin = 8.0F;
constexpr float kListWidth = 218.0F;
constexpr float kListRow = 17.0F;
constexpr float kListPaddingX = 10.0F;
constexpr float kListPaddingY = 7.0F;
constexpr float kListText = 12.0F;
constexpr float kGraphText = 11.0F;
constexpr float kGraphLegendRow = 16.0F;
constexpr float kPlotLeft = 10.0F;
constexpr float kPlotRight = 98.0F;
constexpr float kPlotTop = 30.0F;
constexpr float kPlotBottom = 22.0F;
// Roboto's average advance, for placing the next legend entry without measuring text.
constexpr float kAverageGlyph = 0.52F;
constexpr std::size_t kLabelCharacters = 16U;

[[nodiscard]] std::array<std::uint8_t, 4> Rgba(EditorColor color, float alpha = 1.0F) noexcept {
    return { color.r, color.g, color.b, static_cast<std::uint8_t>(std::clamp(alpha, 0.0F, 1.0F) * 255.0F + 0.5F) };
}

// Where a box of the given size sits in the viewport for a corner.
[[nodiscard]] std::array<float, 2> Anchor(ProfilerCorner corner, float width, float height,
    float viewportWidth, float viewportHeight) noexcept {
    const bool right = corner == ProfilerCorner::TopRight || corner == ProfilerCorner::BottomRight;
    const bool bottom = corner == ProfilerCorner::BottomLeft || corner == ProfilerCorner::BottomRight;
    return { right ? viewportWidth - kMargin - width : kMargin, bottom ? viewportHeight - kMargin - height : kMargin };
}

// Whole seconds, but a short history that is not a whole number of them is told in tenths.
[[nodiscard]] std::string Seconds(double seconds) {
    const bool whole = seconds >= 10.0 || std::abs(seconds - std::round(seconds)) < 0.05;
    return whole ? std::format("{:.0f}", seconds) : std::format("{:.1f}", seconds);
}

[[nodiscard]] std::size_t Characters(std::string_view text) noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(text, [](char c) { return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U; }));
}

// A long name (a system of the scene) is cut to what a row holds.
[[nodiscard]] std::string Shorten(std::string_view text) {
    if (Characters(text) <= kLabelCharacters) return std::string{ text };
    std::size_t end = 0U;
    for (std::size_t characters = 0U; end < text.size(); ++end) {
        if ((static_cast<unsigned char>(text[end]) & 0xC0U) != 0x80U && characters++ == kLabelCharacters - 1U) break;
    }
    return std::string{ text.substr(0U, end) } + "\xE2\x80\xA6";
}

void AppendList(ProfilerOverlayContent& content, const ProfilerOverlayModel& model,
    const ProfilerOverlayOptions& options, const EditorTheme& theme, float scale, float viewportWidth, float viewportHeight) {
    const float width = kListWidth * scale;
    const float height = (static_cast<float>(model.lines.size()) * kListRow + kListPaddingY * 2.0F) * scale;
    const auto [left, top] = Anchor(options.listCorner, width, height, viewportWidth, viewportHeight);
    content.shapes.push_back(Shape{ .kind = Shape::Kind::RoundedRect, .points = { { left, top }, { left + width, top + height } },
        .radius = 5.0F * scale, .color = Rgba(theme.menuBar, static_cast<float>(options.opacityPercent) / 100.0F) });
    float rowTop = top + kListPaddingY * scale;
    for (const ProfilerOverlayLine& line : model.lines) {
        const float textTop = rowTop + (kListRow - kListText * 1.17F) * 0.5F * scale;
        content.labels.push_back(Label{ .x = left + kListPaddingX * scale, .y = textTop, .pixelHeight = kListText * scale,
            .text = Shorten(line.label), .color = Rgba(theme.textSecondary), .shadow = false });
        content.labels.push_back(Label{ .x = left + width - kListPaddingX * scale, .y = textTop, .pixelHeight = kListText * scale,
            .text = line.available ? line.value : std::string{ EditorText("profiler.unavailable", "unavailable") },
            .color = Rgba(line.available ? profiler_palette::Severity(theme, line.severity) : theme.textDisabled),
            .align = Align::Right, .shadow = false });
        rowTop += kListRow * scale;
    }
}

void AppendGraph(ProfilerOverlayContent& content, const ProfilerOverlayModel& model,
    const ProfilerOverlayOptions& options, const EditorTheme& theme, const ProfilerOverlayMetrics& metrics,
    float viewportWidth, float viewportHeight) {
    const float scale = metrics.scale;
    const float width = metrics.graphWidth;
    const float height = metrics.graphHeight;
    const auto [left, top] = Anchor(options.graphCorner, width, height, viewportWidth, viewportHeight);
    content.shapes.push_back(Shape{ .kind = Shape::Kind::RoundedRect, .points = { { left, top }, { left + width, top + height } },
        .radius = 5.0F * scale, .color = Rgba(theme.menuBar, static_cast<float>(options.opacityPercent) / 100.0F) });

    // Legend: a swatch and the name and latest reading of each line.
    const float textHeight = kGraphText * scale;
    const float legendTop = top + kListPaddingY * scale;
    float legendX = left + kPlotLeft * scale;
    for (const ProfilerGraphSeries& series : model.series) {
        const char* name = series.role == ProfilerSeriesRole::Frame ? "Frame"
            : series.role == ProfilerSeriesRole::RenderThread ? "Render thread" : "GPU";
        const char* key = series.role == ProfilerSeriesRole::Frame ? "profiler.line.frame"
            : series.role == ProfilerSeriesRole::RenderThread ? "profiler.line.render" : "profiler.line.gpu";
        const std::string text = std::format("{} {:.1f}", EditorText(key, name), series.latest);
        const float swatch = 8.0F * scale;
        content.shapes.push_back(Shape{ .kind = Shape::Kind::RoundedRect,
            .points = { { legendX, legendTop + (kGraphLegendRow * scale - swatch) * 0.5F }, { legendX + swatch, legendTop + (kGraphLegendRow * scale + swatch) * 0.5F } },
            .radius = 2.0F * scale, .color = Rgba(profiler_palette::Series(theme, series.role)) });
        content.labels.push_back(Label{ .x = legendX + swatch + 5.0F * scale, .y = legendTop + (kGraphLegendRow * scale - textHeight * 1.17F) * 0.5F,
            .pixelHeight = textHeight, .text = text, .color = Rgba(theme.textSecondary), .shadow = false });
        legendX += swatch + 5.0F * scale + static_cast<float>(Characters(text)) * kAverageGlyph * textHeight + 12.0F * scale;
    }
    if (model.spanSeconds >= 1.0) {
        content.labels.push_back(Label{ .x = left + width - kListPaddingX * scale, .y = legendTop + (kGraphLegendRow * scale - textHeight * 1.17F) * 0.5F,
            .pixelHeight = textHeight,
            .text = EditorLocalization::Format("profiler.graph.span", "last {seconds} s", { { "seconds", Seconds(model.spanSeconds) } }),
            .color = Rgba(theme.textDisabled), .align = Align::Right, .shadow = false });
    }

    // Plot: axes, the threshold lines and one polyline per series, the newest reading at the right.
    const float plotLeft = left + kPlotLeft * scale;
    const float plotRight = left + width - kPlotRight * scale;
    const float plotTop = top + kPlotTop * scale;
    const float plotBottom = top + height - kPlotBottom * scale;
    const auto level = [&](double milliseconds) {
        return plotBottom - static_cast<float>(std::clamp(milliseconds / model.axisTopMs, 0.0, 1.0)) * (plotBottom - plotTop);
    };
    content.shapes.push_back(Shape{ .points = { { plotLeft, plotTop }, { plotLeft, plotBottom }, { plotRight, plotBottom } },
        .color = Rgba(theme.borderPanel) });
    const auto threshold = [&](double milliseconds, EditorColor color, const char* key, const char* name) {
        if (milliseconds <= 0.0) return;
        const float y = level(milliseconds);
        content.shapes.push_back(Shape{ .points = { { plotLeft, y }, { plotRight, y } }, .dashed = true, .color = Rgba(color) });
        content.labels.push_back(Label{ .x = plotRight + 8.0F * scale, .y = y - 7.0F * scale, .pixelHeight = 10.5F * scale,
            .text = std::format("{:.1f} ms ({})", milliseconds, EditorText(key, name)), .color = Rgba(color), .shadow = false });
    };
    threshold(model.alarmMs, profiler_palette::kAlarm, "profiler.line.alarm", "alarm");
    threshold(model.budgetMs, profiler_palette::kBudget, "profiler.line.budget", "budget");
    const float step = (plotRight - plotLeft) / static_cast<float>(kb::core::ProfilerRegistry::HistoryFrames() - 1U);
    for (const ProfilerGraphSeries& series : model.series) {
        Shape line{ .thickness = 1.4F, .color = Rgba(profiler_palette::Series(theme, series.role)) };
        for (std::size_t index = 0U; index < series.samples.size(); ++index) {
            const float x = plotRight - static_cast<float>(series.samples.size() - 1U - index) * step;
            line.points.push_back({ x, level(series.samples[index]) });
        }
        if (line.points.size() >= 2U) content.shapes.push_back(std::move(line));
    }

    // Time: what the history covers, ending at now.
    const float timeTop = plotBottom + 4.0F * scale;
    const float timeHeight = 10.0F * scale;
    if (model.spanSeconds >= 1.0) {
        content.labels.push_back(Label{ .x = plotLeft, .y = timeTop, .pixelHeight = timeHeight,
            .text = "-" + Seconds(model.spanSeconds) + " s", .color = Rgba(theme.textDisabled), .shadow = false });
        content.labels.push_back(Label{ .x = (plotLeft + plotRight) * 0.5F - 8.0F * scale, .y = timeTop, .pixelHeight = timeHeight,
            .text = "-" + Seconds(model.spanSeconds * 0.5) + " s", .color = Rgba(theme.textDisabled), .shadow = false });
    }
    content.labels.push_back(Label{ .x = plotRight, .y = timeTop, .pixelHeight = timeHeight,
        .text = std::string{ EditorText("profiler.graph.now", "now") }, .color = Rgba(theme.textDisabled),
        .align = Align::Right, .shadow = false });
}

} // namespace

ProfilerOverlayMetrics ProfilerOverlayMetrics::ForTextSize(ProfilerTextSize size) noexcept {
    const float scale = size == ProfilerTextSize::Small ? 1.0F : size == ProfilerTextSize::Medium ? 1.25F : 1.5F;
    return ProfilerOverlayMetrics{ .scale = scale, .graphWidth = 372.0F * scale, .graphHeight = 186.0F * scale };
}

void AppendProfilerOverlay(ProfilerOverlayContent& content, const ProfilerOverlayModel& model,
    const ProfilerOverlayOptions& options, const EditorTheme& theme, const ProfilerOverlayMetrics& metrics,
    float viewportWidth, float viewportHeight) {
    if (!model.lines.empty()) {
        AppendList(content, model, options, theme, metrics.scale, viewportWidth, viewportHeight);
    }
    if (!model.series.empty() || model.budgetMs > 0.0 || model.alarmMs > 0.0) {
        AppendGraph(content, model, options, theme, metrics, viewportWidth, viewportHeight);
    }
}

} // namespace kb::editor
