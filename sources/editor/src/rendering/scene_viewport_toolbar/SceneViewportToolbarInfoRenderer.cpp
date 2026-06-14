#include "rendering/scene_viewport_toolbar/SceneViewportToolbarInfoRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarDrawing.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarMetrics.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarState.hpp"

#include <algorithm>
#include <cstdio>

namespace kb::editor {
namespace {

[[nodiscard]] const char* OnOff(bool value) noexcept {
    return value ? "on" : "off";
}

[[nodiscard]] COLORREF StatusFill(bool active) noexcept {
    return active ? RGB(34, 79, 65) : RGB(39, 42, 48);
}

[[nodiscard]] COLORREF StatusBorder(bool active) noexcept {
    return active ? RGB(63, 154, 116) : RGB(64, 69, 78);
}

[[nodiscard]] COLORREF StatusText(bool active) noexcept {
    return active ? RGB(203, 248, 223) : RGB(158, 166, 176);
}

void DrawCenteredChipText(HDC dc, RECT rect, const char* label, COLORREF color) {
    ScopedFont font(10, FW_SEMIBOLD);
    const ScopedGdiObject selectedFont(dc, font.handle);
    const int previousBkMode = SetBkMode(dc, TRANSPARENT);
    const COLORREF previousTextColor = SetTextColor(dc, color);
    DrawTextA(dc, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SetTextColor(dc, previousTextColor);
    SetBkMode(dc, previousBkMode);
}

void DrawStatusChip(HDC dc, RECT rect, const char* label, bool active) {
    SceneViewportToolbarDrawing::FillRound(dc, rect, StatusFill(active), StatusBorder(active), 4);
    DrawCenteredChipText(dc, rect, label, StatusText(active));
}

void DrawMetricChip(HDC dc, RECT rect, const char* label, COLORREF fill, COLORREF border, COLORREF text) {
    SceneViewportToolbarDrawing::FillRound(dc, rect, fill, border, 4);
    DrawCenteredChipText(dc, rect, label, text);
}

[[nodiscard]] RECT InfoChipRect(const RECT& parent, int& cursor, int width) noexcept {
    const int top = parent.top + ((parent.bottom - parent.top - SceneViewportToolbarMetrics::InfoChipHeight) / 2);
    RECT rect{ cursor, top, cursor + width, top + SceneViewportToolbarMetrics::InfoChipHeight };
    cursor = rect.right + SceneViewportToolbarMetrics::InfoChipGap;
    return rect;
}

void DrawTooltipLine(HDC dc, RECT rect, const char* text, COLORREF color, int weight = FW_NORMAL) {
    ScopedFont font(11, weight);
    const ScopedGdiObject selectedFont(dc, font.handle);
    const int previousBkMode = SetBkMode(dc, TRANSPARENT);
    const COLORREF previousTextColor = SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SetTextColor(dc, previousTextColor);
    SetBkMode(dc, previousBkMode);
}

void PaintSubmissionTooltip(HDC dc, RECT line, const SceneViewportToolbarRenderStats& stats) {
    constexpr int lineHeight = 18;
    constexpr int headerHeight = 20;
    DrawTooltipLine(dc, line, "Scene submission", RGB(226, 234, 244), FW_SEMIBOLD);
    line.top += headerHeight;
    line.bottom = line.top + lineHeight;

    char submitted[96]{};
    std::snprintf(submitted, sizeof(submitted), "Draw calls: %u    Meshes: %u", stats.submittedDrawCalls, stats.submittedMeshes);
    DrawTooltipLine(dc, line, submitted, RGB(198, 208, 220));
    line.top += lineHeight;
    line.bottom = line.top + lineHeight;

    char gpu[96]{};
    std::snprintf(gpu, sizeof(gpu), "GPU driven: %s    Dispatches: %u", OnOff(stats.gpuDrivenActive || stats.gpuDispatches != 0U), stats.gpuDispatches);
    DrawTooltipLine(dc, line, gpu, RGB(198, 208, 220));
    line.top += lineHeight;
    line.bottom = line.top + lineHeight;
    DrawTooltipLine(dc, line, "These values come from the last submitted scene frame.", RGB(132, 144, 158));
}

void PaintPipelineTooltip(HDC dc, RECT line, const SceneViewportToolbarRenderStats& stats) {
    constexpr int lineHeight = 18;
    constexpr int headerHeight = 20;
    DrawTooltipLine(dc, line, "Scene render pipeline", RGB(226, 234, 244), FW_SEMIBOLD);
    line.top += headerHeight;
    line.bottom = line.top + lineHeight;

    char fx[96]{};
    std::snprintf(fx, sizeof(fx), "Post process: %s    Final composite: %s", OnOff(stats.postProcessActive), OnOff(stats.finalCompositeActive));
    DrawTooltipLine(dc, line, fx, RGB(198, 208, 220));
    line.top += lineHeight;
    line.bottom = line.top + lineHeight;

    char aa[96]{};
    if (stats.msaaSamples > 0U) {
        std::snprintf(aa, sizeof(aa), "TAA: %s    MSAA: %ux", OnOff(stats.temporalAntiAliasingActive), static_cast<unsigned>(stats.msaaSamples));
    } else {
        std::snprintf(aa, sizeof(aa), "TAA: %s    MSAA: off", OnOff(stats.temporalAntiAliasingActive));
    }
    DrawTooltipLine(dc, line, aa, RGB(198, 208, 220));
    line.top += lineHeight;
    line.bottom = line.top + lineHeight;

    char bloom[96]{};
    std::snprintf(bloom, sizeof(bloom), "Bloom: %s", OnOff(stats.bloomActive));
    DrawTooltipLine(dc, line, bloom, RGB(198, 208, 220));
}

} // namespace

void SceneViewportToolbarInfoRenderer::PaintFpsCounter(HDC dc, RECT rect, const EditorTheme& theme) {
    const COLORREF fill = SceneViewportToolbarDrawing::Blend(SceneViewportToolbarDrawing::ToolbarRowColor(theme), RGB(0, 0, 0), 1, 5);
    const COLORREF border = SceneViewportToolbarDrawing::Blend(GdiDrawing::ToColorRef(theme.borderPanel), RGB(96, 109, 132), 1, 8);
    SceneViewportToolbarDrawing::FillRound(dc, rect, fill, border, SceneViewportToolbarMetrics::ButtonRadius);

    char text[16]{};
    const int fps = SceneViewportToolbarState::CurrentPresentedFps();
    if (fps > 0) {
        std::snprintf(text, sizeof(text), "FPS %d", fps);
    } else {
        std::snprintf(text, sizeof(text), "FPS --");
    }

    ScopedFont font(11, FW_SEMIBOLD);
    const ScopedGdiObject selectedFont(dc, font.handle);
    const int previousBkMode = SetBkMode(dc, TRANSPARENT);
    const COLORREF previousTextColor = SetTextColor(dc, GdiDrawing::ToColorRef(theme.textSecondary));
    DrawTextA(dc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SetTextColor(dc, previousTextColor);
    SetBkMode(dc, previousBkMode);
}

void SceneViewportToolbarInfoRenderer::PaintRenderStats(HDC dc, RECT rect, const EditorTheme& theme) {
    const COLORREF fill = SceneViewportToolbarDrawing::Blend(SceneViewportToolbarDrawing::ToolbarRowColor(theme), RGB(0, 0, 0), 1, 5);
    const COLORREF border = SceneViewportToolbarDrawing::Blend(GdiDrawing::ToColorRef(theme.borderPanel), RGB(96, 109, 132), 1, 8);
    SceneViewportToolbarDrawing::FillRound(dc, rect, fill, border, SceneViewportToolbarMetrics::ButtonRadius);

    const SceneViewportToolbarRenderStats stats = SceneViewportToolbarState::RenderStats();
    RECT inner = GdiDrawing::Inset(rect, 4);
    int cursor = inner.left;
    char drawCalls[16]{};
    char meshes[16]{};
    std::snprintf(drawCalls, sizeof(drawCalls), "DC %u", stats.submittedDrawCalls);
    std::snprintf(meshes, sizeof(meshes), "M %u", stats.submittedMeshes);
    DrawMetricChip(dc, InfoChipRect(inner, cursor, 42), drawCalls, RGB(35, 45, 60), RGB(61, 78, 102), RGB(198, 214, 234));
    DrawMetricChip(dc, InfoChipRect(inner, cursor, 36), meshes, RGB(36, 48, 45), RGB(65, 88, 78), RGB(201, 228, 214));
    DrawStatusChip(dc, InfoChipRect(inner, cursor, std::max<int>(38, static_cast<int>(inner.right - cursor))), "GPU", stats.gpuDrivenActive || stats.gpuDispatches != 0U);
}

void SceneViewportToolbarInfoRenderer::PaintPipelineStats(HDC dc, RECT rect, const EditorTheme& theme) {
    const COLORREF fill = SceneViewportToolbarDrawing::Blend(SceneViewportToolbarDrawing::ToolbarRowColor(theme), RGB(0, 0, 0), 1, 5);
    const COLORREF border = SceneViewportToolbarDrawing::Blend(GdiDrawing::ToColorRef(theme.borderPanel), RGB(96, 109, 132), 1, 8);
    SceneViewportToolbarDrawing::FillRound(dc, rect, fill, border, SceneViewportToolbarMetrics::ButtonRadius);

    const SceneViewportToolbarRenderStats stats = SceneViewportToolbarState::RenderStats();
    RECT inner = GdiDrawing::Inset(rect, 4);
    int cursor = inner.left;
    DrawStatusChip(dc, InfoChipRect(inner, cursor, 32), "FX", stats.postProcessActive && stats.finalCompositeActive);
    DrawStatusChip(dc, InfoChipRect(inner, cursor, 38), "TAA", stats.temporalAntiAliasingActive);
    DrawStatusChip(dc, InfoChipRect(inner, cursor, 54), "Bloom", stats.bloomActive);
    DrawStatusChip(dc, InfoChipRect(inner, cursor, std::max<int>(48, static_cast<int>(inner.right - cursor))), "MSAA", stats.msaaSamples > 0U);
}

void SceneViewportToolbarInfoRenderer::PaintTooltip(HDC dc, const RECT& content, const SceneViewportToolbarRects& rects) {
    const SceneViewportToolbarInfoHover hover = SceneViewportToolbarState::InfoHover();
    if (hover == SceneViewportToolbarInfoHover::None) {
        return;
    }

    const SceneViewportToolbarRenderStats stats = SceneViewportToolbarState::RenderStats();
    const RECT anchor = hover == SceneViewportToolbarInfoHover::RenderStats ? rects.renderStats : rects.pipelineStats;
    constexpr int width = 282;
    constexpr int lineHeight = 18;
    constexpr int headerHeight = 20;
    constexpr int lineCount = 4;
    const int height = SceneViewportToolbarMetrics::TooltipPaddingY * 2 + headerHeight + (lineHeight * lineCount);
    const int left = std::clamp(
        anchor.left,
        content.left + SceneViewportToolbarMetrics::PaddingX,
        std::max(content.left + SceneViewportToolbarMetrics::PaddingX, content.right - width - SceneViewportToolbarMetrics::PaddingX));
    RECT popup{ left, rects.toolbar.bottom + 5, left + width, rects.toolbar.bottom + 5 + height };
    if (popup.bottom > content.bottom - 6) {
        popup.top = anchor.top - 5 - height;
        popup.bottom = anchor.top - 5;
    }

    SceneViewportToolbarDrawing::FillRound(dc, popup, RGB(22, 25, 30), RGB(72, 88, 110), 7);
    GdiDrawing::FillRectColor(dc, RECT{ popup.left + 2, popup.top + 1, popup.right - 2, popup.top + 2 }, RGB(56, 68, 86));

    RECT line{
        popup.left + SceneViewportToolbarMetrics::TooltipPaddingX,
        popup.top + SceneViewportToolbarMetrics::TooltipPaddingY,
        popup.right - SceneViewportToolbarMetrics::TooltipPaddingX,
        popup.top + SceneViewportToolbarMetrics::TooltipPaddingY + headerHeight,
    };
    if (hover == SceneViewportToolbarInfoHover::RenderStats) {
        PaintSubmissionTooltip(dc, line, stats);
        return;
    }
    PaintPipelineTooltip(dc, line, stats);
}

} // namespace kb::editor

#endif
