#include "rendering/SceneViewportToolbarRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconKind.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarDrawing.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarInfoRenderer.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarLayout.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarState.hpp"
#include "scene/EditorSceneContext.hpp"
#include "scene/EditorTerrainService.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>
#include <utility>

namespace kb::editor {
namespace {

[[nodiscard]] const char* TerrainBrushLabel(kb::terrain_editor::TerrainBrushMode mode) noexcept {
    using Mode = kb::terrain_editor::TerrainBrushMode;
    switch (mode) {
    case Mode::Raise: return "Raise / Mountain";
    case Mode::Lower: return "Lower / Valley";
    case Mode::Smooth: return "Smooth";
    case Mode::Flatten: return "Flatten";
    case Mode::Noise: return "Noise";
    case Mode::Terrace: return "Terrace";
    case Mode::CutHole: return "Cut Hole";
    case Mode::FillHole: return "Fill Hole";
    }
    return "Raise / Mountain";
}

void DrawTerrainModeButton(
    HDC dc,
    const RECT& rect,
    const char* label,
    const EditorTheme& theme,
    bool active) {
    const COLORREF fill = active ? RGB(36, 91, 111) : RGB(29, 34, 41);
    const COLORREF border = active ? RGB(91, 190, 220) : RGB(59, 68, 80);
    SceneViewportToolbarDrawing::FillRound(dc, rect, fill, border, 7);
    GdiDrawing::DrawCenteredText(
        dc, rect, label,
        active ? RGB(239, 252, 255) : GdiDrawing::ToColorRef(theme.textSecondary));
}

void DrawTerrainValue(
    HDC dc,
    const RECT& rect,
    const char* text,
    const EditorTheme& theme) {
    SceneViewportToolbarDrawing::FillRound(
        dc, rect, RGB(22, 26, 32), RGB(52, 62, 74), 6);
    GdiDrawing::DrawCenteredText(dc, rect, text, GdiDrawing::ToColorRef(theme.textPrimary));
}

} // namespace

SceneViewportToolbarRects SceneViewportToolbarRenderer::Resolve(const RECT& content) noexcept {
    return SceneViewportToolbarLayout::Resolve(content);
}

SceneViewportToolbarRects SceneViewportToolbarRenderer::Resolve(const RECT& content, const EditorViewportPreviewState& state) noexcept {
    return SceneViewportToolbarLayout::Resolve(content, state);
}

SceneViewportToolbarRects SceneViewportToolbarRenderer::Resolve(
    const RECT& content,
    const EditorViewportPreviewState& state,
    const EditorSceneContext& sceneContext) noexcept {
    SceneViewportToolbarRects rects = SceneViewportToolbarLayout::Resolve(content, state);
    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();
    if (sceneContext.IsProjectPluginEnabled("Editor.Terrain") &&
        EditorTerrainService::IsTerrainEntity(sceneContext.Scene(), selected)) {
        rects.renderArea.top = std::min<LONG>(
            rects.renderArea.bottom,
            rects.renderArea.top + TerrainToolsInset);
    }
    return rects;
}

TerrainViewportToolbarRects SceneViewportToolbarRenderer::ResolveTerrainTools(const RECT& content) noexcept {
    return SceneViewportToolbarLayout::ResolveTerrainTools(content);
}

void SceneViewportToolbarRenderer::RecordPresentedFrame() noexcept {
    SceneViewportToolbarState::RecordPresentedFrame();
}

void SceneViewportToolbarRenderer::RecordRenderStats(SceneViewportToolbarRenderStats stats) noexcept {
    SceneViewportToolbarState::RecordRenderStats(stats);
}

void SceneViewportToolbarRenderer::RecordEcsStats(SceneViewportToolbarEcsStats stats) {
    SceneViewportToolbarState::RecordEcsStats(std::move(stats));
}

bool SceneViewportToolbarRenderer::UpdateInfoHover(const RECT& content, int x, int y) noexcept {
    return SceneViewportToolbarState::UpdateInfoHover(content, x, y);
}

void SceneViewportToolbarRenderer::Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorViewportPreviewState& state) {
    const SceneViewportToolbarRects rects = SceneViewportToolbarLayout::Resolve(content, state);
    GdiDrawing::FillRectColor(dc, rects.row, SceneViewportToolbarDrawing::ToolbarRowColor(theme));

    SceneViewportToolbarInfoRenderer::PaintFpsCounter(dc, rects.fpsCounter, theme);
    SceneViewportToolbarInfoRenderer::PaintRenderStats(dc, rects.renderStats, theme);
    SceneViewportToolbarInfoRenderer::PaintEcsStats(dc, rects.ecsStats, theme);

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

void SceneViewportToolbarRenderer::PaintTerrainTools(
    HDC dc,
    const RECT& content,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext) {
    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();
    if (!sceneContext.IsProjectPluginEnabled("Editor.Terrain") ||
        !EditorTerrainService::IsTerrainEntity(sceneContext.Scene(), selected)) {
        return;
    }

    const TerrainViewportToolbarRects rects = ResolveTerrainTools(content);
    const EditorTerrainToolState& tool = EditorTerrainService::ToolState();
    SceneViewportToolbarDrawing::FillRound(
        dc, rects.panel, RGB(20, 24, 30), RGB(67, 78, 92), 9);
    GdiDrawing::FillRectColor(
        dc,
        RECT{ rects.panel.left + 2, rects.panel.top + 8,
              rects.panel.left + 4, rects.panel.bottom - 8 },
        RGB(80, 190, 222));

    const EditorTerrainToolMode visibleMode = tool.editingEnabled
        ? tool.mode
        : EditorTerrainToolMode::Select;
    DrawTerrainModeButton(dc, rects.selectButton, "Select", theme, visibleMode == EditorTerrainToolMode::Select);
    DrawTerrainModeButton(dc, rects.sculptButton, "Sculpt", theme, visibleMode == EditorTerrainToolMode::Sculpt);
    DrawTerrainModeButton(dc, rects.holesButton, "Holes", theme, visibleMode == EditorTerrainToolMode::Holes);
    SceneViewportToolbarDrawing::DrawValueButton(
        dc, rects.brushButton, HeroIconKind::AdjustmentsHorizontal,
        TerrainBrushLabel(tool.brush.mode), theme, tool.brushMenuOpen);

    SceneViewportToolbarDrawing::DrawIconButton(dc, rects.sizeMinusButton, HeroIconKind::Minus, theme, false);
    std::array<char, 32U> sizeText{};
    std::snprintf(sizeText.data(), sizeText.size(), "Size %.2g", static_cast<double>(tool.brush.radius));
    DrawTerrainValue(dc, rects.sizeValue, sizeText.data(), theme);
    SceneViewportToolbarDrawing::DrawIconButton(dc, rects.sizePlusButton, HeroIconKind::Plus, theme, false);

    SceneViewportToolbarDrawing::DrawIconButton(dc, rects.strengthMinusButton, HeroIconKind::Minus, theme, false);
    std::array<char, 40U> strengthText{};
    std::snprintf(strengthText.data(), strengthText.size(), "Strength %.2g", static_cast<double>(tool.brush.strength));
    DrawTerrainValue(dc, rects.strengthValue, strengthText.data(), theme);
    SceneViewportToolbarDrawing::DrawIconButton(dc, rects.strengthPlusButton, HeroIconKind::Plus, theme, false);

}

} // namespace kb::editor

#endif
