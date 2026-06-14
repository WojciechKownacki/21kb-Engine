#include "rendering/SceneViewportToolbarRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconKind.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarDrawing.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarInfoRenderer.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarLayout.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarState.hpp"

namespace kb::editor {

SceneViewportToolbarRects SceneViewportToolbarRenderer::Resolve(const RECT& content) noexcept {
    return SceneViewportToolbarLayout::Resolve(content);
}

SceneViewportToolbarRects SceneViewportToolbarRenderer::Resolve(const RECT& content, const EditorViewportPreviewState& state) noexcept {
    return SceneViewportToolbarLayout::Resolve(content, state);
}

void SceneViewportToolbarRenderer::RecordPresentedFrame() noexcept {
    SceneViewportToolbarState::RecordPresentedFrame();
}

void SceneViewportToolbarRenderer::RecordRenderStats(SceneViewportToolbarRenderStats stats) noexcept {
    SceneViewportToolbarState::RecordRenderStats(stats);
}

bool SceneViewportToolbarRenderer::UpdateInfoHover(const RECT& content, int x, int y) noexcept {
    return SceneViewportToolbarState::UpdateInfoHover(content, x, y);
}

void SceneViewportToolbarRenderer::Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorViewportPreviewState& state) {
    const SceneViewportToolbarRects rects = SceneViewportToolbarLayout::Resolve(content, state);
    GdiDrawing::FillRectColor(dc, rects.row, SceneViewportToolbarDrawing::ToolbarRowColor(theme));

    SceneViewportToolbarInfoRenderer::PaintFpsCounter(dc, rects.fpsCounter, theme);
    SceneViewportToolbarInfoRenderer::PaintRenderStats(dc, rects.renderStats, theme);
    SceneViewportToolbarInfoRenderer::PaintPipelineStats(dc, rects.pipelineStats, theme);

    SceneViewportToolbarDrawing::DrawValueButton(
        dc,
        rects.renderProfileButton,
        HeroIconKind::Gamepad2,
        EditorViewportRenderProfileLabel(state.RenderProfile()),
        theme);
    SceneViewportToolbarDrawing::DrawDivider(dc, rects.toolbar, rects.renderProfileButton.right + 7, theme);
    SceneViewportToolbarDrawing::DrawIconButton(dc, rects.gridToggleButton, HeroIconKind::Eye, theme, state.GridVisible());
    SceneViewportToolbarDrawing::DrawValueButton(
        dc,
        rects.gridStepButton,
        HeroIconKind::AdjustmentsHorizontal,
        EditorViewportGridSpacingLabel(state.GridSpacing()),
        theme);
    SceneViewportToolbarDrawing::DrawDivider(dc, rects.toolbar, rects.gridStepButton.right + 7, theme);
    SceneViewportToolbarDrawing::DrawIconButton(dc, rects.snapToggleButton, HeroIconKind::Cube, theme, state.SnapEnabled());
    SceneViewportToolbarDrawing::DrawValueButton(
        dc,
        rects.snapStepButton,
        HeroIconKind::AdjustmentsHorizontal,
        EditorViewportSnapStepLabel(state.SnapStep()),
        theme);
    SceneViewportToolbarDrawing::DrawValueButton(
        dc,
        rects.rotationSnapButton,
        HeroIconKind::RotationSnap,
        EditorViewportRotationSnapLabel(state.RotationSnapDegrees()),
        theme,
        state.RotationSnapDegrees() > 0.0F);

    SceneViewportToolbarInfoRenderer::PaintTooltip(dc, content, rects);
}

} // namespace kb::editor

#endif
