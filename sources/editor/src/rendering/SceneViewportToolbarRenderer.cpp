#include "rendering/ProjectFilesAssetIconResolver.hpp"
#include "localization/EditorLocalization.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconKind.hpp"
#include "rendering/components/EditorDialogStyle.hpp"
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

[[nodiscard]] std::string_view TerrainBrushLabel(kb::terrain_editor::TerrainBrushMode mode) {
    using Mode = kb::terrain_editor::TerrainBrushMode;
    switch (mode) {
    case Mode::Raise: return EditorText("terrainTool.brush.raise", "Raise / Mountain");
    case Mode::Lower: return EditorText("terrainTool.brush.lower", "Lower / Valley");
    case Mode::Smooth: return EditorText("terrainTool.brush.smooth", "Smooth");
    case Mode::Flatten: return EditorText("terrainTool.brush.flatten", "Flatten");
    case Mode::Noise: return EditorText("terrainTool.brush.noise", "Noise");
    case Mode::Terrace: return EditorText("terrainTool.brush.terrace", "Terrace");
    case Mode::CutHole: return EditorText("terrainTool.brush.cutHole", "Cut Hole");
    case Mode::FillHole: return EditorText("terrainTool.brush.fillHole", "Fill Hole");
    }
    return EditorText("terrainTool.brush.raise", "Raise / Mountain");
}

[[nodiscard]] std::string_view TerrainBrushDescription(kb::terrain_editor::TerrainBrushMode mode) {
    using Mode = kb::terrain_editor::TerrainBrushMode;
    switch (mode) {
    case Mode::Raise: return EditorText("terrainTool.brushHint.raise", "Build height");
    case Mode::Lower: return EditorText("terrainTool.brushHint.lower", "Carve terrain");
    case Mode::Smooth: return EditorText("terrainTool.brushHint.smooth", "Soften details");
    case Mode::Flatten: return EditorText("terrainTool.brushHint.flatten", "Level to height");
    case Mode::Noise: return EditorText("terrainTool.brushHint.noise", "Add texture");
    case Mode::Terrace: return EditorText("terrainTool.brushHint.terrace", "Create steps");
    case Mode::CutHole: return EditorText("terrainTool.brushHint.cutHole", "Remove surface");
    case Mode::FillHole: return EditorText("terrainTool.brushHint.fillHole", "Restore surface");
    }
    return {};
}

[[nodiscard]] std::string_view TerrainBrushToolbarLabel(kb::terrain_editor::TerrainBrushMode mode) {
    using Mode = kb::terrain_editor::TerrainBrushMode;
    switch (mode) {
    case Mode::Raise: return EditorText("terrainTool.brushShort.raise", "Raise");
    case Mode::Lower: return EditorText("terrainTool.brushShort.lower", "Lower");
    default: return TerrainBrushLabel(mode);
    }
}

[[nodiscard]] std::string_view TerrainBrushShapeLabel(kb::terrain_editor::TerrainBrushShape shape) {
    using Shape = kb::terrain_editor::TerrainBrushShape;
    switch (shape) {
    case Shape::SoftRound: return EditorText("terrainTool.shape.softRound", "Soft Round");
    case Shape::HardRound: return EditorText("terrainTool.shape.hardRound", "Hard Round");
    case Shape::LinearRound: return EditorText("terrainTool.shape.linearRound", "Linear");
    case Shape::Bell: return EditorText("terrainTool.shape.bell", "Bell");
    case Shape::Ring: return EditorText("terrainTool.shape.ring", "Ring");
    case Shape::Speckle: return EditorText("terrainTool.shape.speckle", "Speckle");
    }
    return EditorText("terrainTool.shape.softRound", "Soft Round");
}

[[nodiscard]] std::string_view TerrainBrushShapeDescription(kb::terrain_editor::TerrainBrushShape shape) {
    using Shape = kb::terrain_editor::TerrainBrushShape;
    switch (shape) {
    case Shape::SoftRound: return EditorText("terrainTool.shapeHint.softRound", "Smooth feathered edge");
    case Shape::HardRound: return EditorText("terrainTool.shapeHint.hardRound", "Solid precise edge");
    case Shape::LinearRound: return EditorText("terrainTool.shapeHint.linearRound", "Even radial fade");
    case Shape::Bell: return EditorText("terrainTool.shapeHint.bell", "Natural dome");
    case Shape::Ring: return EditorText("terrainTool.shapeHint.ring", "Circular band");
    case Shape::Speckle: return EditorText("terrainTool.shapeHint.speckle", "Organic texture");
    }
    return {};
}

void DrawTextAt(HDC dc, RECT rect, const char* text, COLORREF color, UINT format) {
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextA(dc, text, -1, &rect, format | DT_NOPREFIX | DT_SINGLELINE);
}

void DrawBrushShapePreview(HDC dc, const RECT& rect, kb::terrain_editor::TerrainBrushShape shape) {
    SceneViewportToolbarDrawing::FillRound(dc, rect, RGB(15, 18, 23), RGB(55, 65, 77), 7);
    const int centerX = (rect.left + rect.right) / 2;
    const int centerY = (rect.top + rect.bottom) / 2;
    const int width = static_cast<int>(rect.right - rect.left);
    const int height = static_cast<int>(rect.bottom - rect.top);
    const int radius = std::max(3, std::min(width, height) / 2 - 5);
    const auto circle = [&](int r, COLORREF color) {
        HBRUSH brush = CreateSolidBrush(color);
        HPEN pen = CreatePen(PS_NULL, 0, color);
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        Ellipse(dc, centerX - r, centerY - r, centerX + r + 1, centerY + r + 1);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
    };
    using Shape = kb::terrain_editor::TerrainBrushShape;
    if (shape == Shape::HardRound) {
        circle(radius, RGB(116, 193, 218));
    } else if (shape == Shape::Ring) {
        circle(radius, RGB(41, 72, 86));
        circle(std::max(1, radius - 4), RGB(116, 193, 218));
        circle(std::max(1, radius - 9), RGB(18, 23, 29));
    } else if (shape == Shape::Speckle) {
        constexpr std::array<POINT, 13U> dots{{
            {-8, -7}, {-1, -9}, {7, -6}, {-10, 1}, {-3, -1}, {5, 0}, {10, 4},
            {-7, 8}, {1, 7}, {7, 10}, {0, 1}, {4, -10}, {-11, -3},
        }};
        for (std::size_t index = 0U; index < dots.size(); ++index) {
            const int dotRadius = index % 3U == 0U ? 2 : 1;
            HBRUSH brush = CreateSolidBrush(index % 2U == 0U ? RGB(117, 197, 222) : RGB(66, 122, 143));
            HGDIOBJ oldBrush = SelectObject(dc, brush);
            Ellipse(dc, centerX + dots[index].x - dotRadius, centerY + dots[index].y - dotRadius,
                centerX + dots[index].x + dotRadius + 1, centerY + dots[index].y + dotRadius + 1);
            SelectObject(dc, oldBrush);
            DeleteObject(brush);
        }
    } else {
        const int layers = shape == Shape::LinearRound ? 4 : 5;
        for (int layer = layers; layer >= 1; --layer) {
            const int r = std::max(1, radius * layer / layers);
            const int intensity = shape == Shape::Bell
                ? 48 + (layers - layer) * 34
                : 42 + (layers - layer) * 30;
            circle(r, RGB(std::min(135, intensity), std::min(205, intensity + 54), std::min(226, intensity + 72)));
        }
    }
}

void DrawOperationGlyph(HDC dc, const RECT& rect, kb::terrain_editor::TerrainBrushMode mode) {
    SceneViewportToolbarDrawing::FillRound(dc, rect, RGB(16, 20, 25), RGB(53, 64, 76), 7);
    const int left = rect.left + 8;
    const int right = rect.right - 8;
    const int top = rect.top + 8;
    const int bottom = rect.bottom - 8;
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(104, 190, 217));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    using Mode = kb::terrain_editor::TerrainBrushMode;
    if (mode == Mode::Flatten) {
        MoveToEx(dc, left, (top + bottom) / 2, nullptr); LineTo(dc, right, (top + bottom) / 2);
    } else if (mode == Mode::Terrace) {
        MoveToEx(dc, left, bottom, nullptr); LineTo(dc, left + 8, bottom); LineTo(dc, left + 8, bottom - 7);
        LineTo(dc, left + 16, bottom - 7); LineTo(dc, left + 16, top); LineTo(dc, right, top);
    } else if (mode == Mode::Smooth) {
        MoveToEx(dc, left, bottom - 3, nullptr); LineTo(dc, left + 7, top + 5); LineTo(dc, left + 14, top + 2); LineTo(dc, right, top + 6);
    } else if (mode == Mode::Noise) {
        constexpr std::array<POINT, 6U> points{{{4,16},{9,7},{14,13},{19,4},{24,11},{29,6}}};
        MoveToEx(dc, rect.left + points[0].x, rect.top + points[0].y, nullptr);
        for (std::size_t i = 1U; i < points.size(); ++i) LineTo(dc, rect.left + points[i].x, rect.top + points[i].y);
    } else if (mode == Mode::CutHole || mode == Mode::FillHole) {
        Ellipse(dc, left + 2, top + 2, right - 1, bottom - 1);
        if (mode == Mode::FillHole) { MoveToEx(dc, (left + right) / 2, top + 5, nullptr); LineTo(dc, (left + right) / 2, bottom - 4); }
    } else {
        const bool lower = mode == Mode::Lower;
        const int peak = lower ? bottom : top;
        const int base = lower ? top : bottom;
        MoveToEx(dc, left, base, nullptr); LineTo(dc, (left + right) / 2, peak); LineTo(dc, right, base);
    }
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void DrawTerrainModeButton(
    HDC dc,
    const RECT& rect,
    std::string_view label,
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
    std::string_view text,
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
    const bool terrainToolsVisible = sceneContext.IsProjectPluginEnabled("Editor.Terrain") &&
        EditorTerrainService::IsTerrainEntity(sceneContext.Scene(), sceneContext.SelectedEntity());
    return SceneViewportToolbarLayout::Resolve(content, state, terrainToolsVisible);
}

TerrainViewportToolbarRects SceneViewportToolbarRenderer::ResolveTerrainTools(const RECT& content) noexcept {
    return SceneViewportToolbarLayout::ResolveTerrainTools(content);
}

void SceneViewportToolbarRenderer::RecordFrameMilliseconds(double milliseconds) noexcept {
    SceneViewportToolbarState::RecordFrameMilliseconds(milliseconds);
}

void SceneViewportToolbarRenderer::Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorViewportPreviewState& state) {
    const SceneViewportToolbarRects rects = SceneViewportToolbarLayout::Resolve(content, state);
    GdiDrawing::FillRectColor(dc, rects.row, SceneViewportToolbarDrawing::ToolbarRowColor(theme));

    SceneViewportToolbarInfoRenderer::PaintFpsCounter(dc, rects.fpsCounter, theme);

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
    SceneViewportToolbarDrawing::FillRound(dc, rects.twoDButton,
        GdiDrawing::ToColorRef(state.Is2D() ? theme.accent : theme.toolbarButton),
        GdiDrawing::ToColorRef(theme.borderPanel), 4);
    GdiDrawing::DrawTabText(dc, rects.twoDButton, "2D", GdiDrawing::ToColorRef(theme.textPrimary));

}

void SceneViewportToolbarRenderer::PaintPrefabEditBar(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    if (!sceneContext.InPrefabEditMode()) {
        return;
    }
    const SceneViewportToolbarRects rects = SceneViewportToolbarLayout::Resolve(content);
    SceneViewportToolbarDrawing::DrawDivider(dc, rects.toolbar, rects.twoDButton.right + 7, theme);
    SceneViewportToolbarDrawing::FillRound(dc, rects.prefabName, ProjectFilesAssetIconResolver::kPrefabColor, GdiDrawing::ToColorRef(theme.borderPanel), 4);
    const std::string name = sceneContext.PrefabEditModeName();
    GdiDrawing::DrawTabText(dc, rects.prefabName, name.c_str(), GdiDrawing::ToColorRef(theme.textPrimary));
    SceneViewportToolbarDrawing::FillRound(dc, rects.prefabSaveButton, GdiDrawing::ToColorRef(theme.toolbarButton), GdiDrawing::ToColorRef(theme.borderPanel), 4);
    GdiDrawing::DrawTabText(dc, rects.prefabSaveButton, EditorText("common.save", "Save"), GdiDrawing::ToColorRef(theme.textPrimary));
    SceneViewportToolbarDrawing::FillRound(dc, rects.prefabCloseButton, GdiDrawing::ToColorRef(theme.toolbarButton), GdiDrawing::ToColorRef(theme.borderPanel), 4);
    GdiDrawing::DrawTabText(dc, rects.prefabCloseButton, EditorText("common.close", "Close"), GdiDrawing::ToColorRef(theme.textPrimary));
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

    const EditorTerrainToolMode visibleMode = tool.editingEnabled
        ? tool.mode
        : EditorTerrainToolMode::Select;
    DrawTerrainModeButton(dc, rects.selectButton, EditorText("terrainTool.select", "Select"), theme, visibleMode == EditorTerrainToolMode::Select);
    DrawTerrainModeButton(dc, rects.sculptButton, EditorText("terrainTool.sculpt", "Sculpt"), theme, visibleMode == EditorTerrainToolMode::Sculpt);
    DrawTerrainModeButton(dc, rects.holesButton, EditorText("terrainTool.holes", "Holes"), theme, visibleMode == EditorTerrainToolMode::Holes);
    DrawTerrainModeButton(dc, rects.paintButton, EditorText("terrainTool.paint", "Paint"), theme, visibleMode == EditorTerrainToolMode::Paint);
    const std::string paintLayerLabel = EditorLocalization::Format("terrainTool.paintLayer", "Layer {index}",
        { { "index", std::to_string(static_cast<unsigned>(tool.selectedMaterialLayer) + 1U) } });
    SceneViewportToolbarDrawing::DrawValueButton(
        dc, rects.brushButton, HeroIconKind::AdjustmentsHorizontal,
        visibleMode == EditorTerrainToolMode::Paint ? std::string_view{ paintLayerLabel } : TerrainBrushToolbarLabel(tool.brush.mode),
        theme, tool.brushMenuOpen);
    SceneViewportToolbarDrawing::DrawValueButton(
        dc, rects.brushShapeButton, HeroIconKind::RectangleGroup,
        TerrainBrushShapeLabel(tool.brush.shape), theme, tool.brushShapeMenuOpen);

    SceneViewportToolbarDrawing::DrawIconButton(dc, rects.sizeMinusButton, HeroIconKind::Minus, theme, false);
    std::array<char, 32U> sizeValue{};
    std::snprintf(sizeValue.data(), sizeValue.size(), "%.2g", static_cast<double>(tool.brush.radius));
    const std::string sizeText = EditorLocalization::Format("terrainTool.size", "Size {value}", { { "value", sizeValue.data() } });
    DrawTerrainValue(dc, rects.sizeValue, sizeText, theme);
    SceneViewportToolbarDrawing::DrawIconButton(dc, rects.sizePlusButton, HeroIconKind::Plus, theme, false);

    SceneViewportToolbarDrawing::DrawIconButton(dc, rects.strengthMinusButton, HeroIconKind::Minus, theme, false);
    std::array<char, 32U> strengthValue{};
    std::snprintf(strengthValue.data(), strengthValue.size(), "%.2g", static_cast<double>(tool.brush.strength));
    const std::string strengthText = visibleMode == EditorTerrainToolMode::Paint
        ? EditorLocalization::Format("terrainTool.opacity", "Opacity {value}", { { "value", strengthValue.data() } })
        : EditorLocalization::Format("terrainTool.strength", "Strength {value}", { { "value", strengthValue.data() } });
    DrawTerrainValue(dc, rects.strengthValue, strengthText, theme);
    SceneViewportToolbarDrawing::DrawIconButton(dc, rects.strengthPlusButton, HeroIconKind::Plus, theme, false);

}

void SceneViewportToolbarRenderer::PaintTerrainPopup(
    HDC dc,
    const RECT& bounds,
    const EditorTheme& theme,
    const EditorSceneContext&,
    int hoveredItem) {
    const EditorTerrainToolState& tool = EditorTerrainService::ToolState();
    const bool shapes = tool.brushShapeMenuOpen;
    if ((!tool.brushMenuOpen && !shapes) || bounds.right <= bounds.left || bounds.bottom <= bounds.top) return;

    EditorDialogStyle::PaintSurface(dc, bounds, theme);
    RECT title{bounds.left + 12, bounds.top + 7, bounds.right - 12, bounds.top + (shapes ? 34 : 30)};
    EditorDialogStyle::PaintText(
        dc,
        title,
        shapes ? EditorText("terrainTool.brushTip", "Brush tip") : EditorText("terrainTool.terrainOperation", "Terrain operation"),
        EditorDialogStyle::Color(theme.textPrimary),
        12,
        FW_SEMIBOLD);

    const int headerHeight = shapes ? 38 : 34;
    const int itemHeight = shapes ? 72 : 55;
    const int columnWidth = shapes ? 196 : 172;
    const int itemWidth = shapes ? 192 : 168;
    const std::size_t count = shapes ? 6U : 8U;
    for (std::size_t index = 0U; index < count; ++index) {
        const int column = static_cast<int>(index % 2U);
        const int row = static_cast<int>(index / 2U);
        const RECT card{
            bounds.left + 6 + column * columnWidth,
            bounds.top + headerHeight + row * itemHeight,
            bounds.left + 6 + column * columnWidth + itemWidth,
            bounds.top + headerHeight + row * itemHeight + (shapes ? 68 : 51),
        };
        const bool selected = shapes
            ? static_cast<std::size_t>(tool.brush.shape) == index
            : static_cast<std::size_t>(tool.brush.mode) == index;
        const bool hovered = hoveredItem == static_cast<int>(index);
        GdiDrawing::FillRectColor(
            dc,
            card,
            selected
                ? EditorDialogStyle::Blend(EditorDialogStyle::Color(theme.panel), EditorDialogStyle::Color(theme.accent), 18)
                : EditorDialogStyle::Color(hovered ? theme.toolbarButton : theme.panel));
        if (selected) {
            GdiDrawing::FillRectColor(
                dc,
                RECT{card.left, card.top, card.left + 3, card.bottom},
                EditorDialogStyle::Color(theme.accent));
        }
        EditorDialogStyle::PaintDivider(dc, RECT{card.left, card.bottom - 1, card.right, card.bottom}, theme);
        const RECT preview{card.left + 7, card.top + 7, card.left + (shapes ? 57 : 45), card.bottom - 7};
        if (shapes) {
            DrawBrushShapePreview(dc, preview, static_cast<kb::terrain_editor::TerrainBrushShape>(index));
        } else {
            DrawOperationGlyph(dc, preview, static_cast<kb::terrain_editor::TerrainBrushMode>(index));
        }
        RECT name{preview.right + 8, card.top + (shapes ? 11 : 7), card.right - 7, card.top + (shapes ? 31 : 26)};
        RECT description{preview.right + 8, name.bottom, card.right - 7, card.bottom - 6};
        EditorDialogStyle::PaintText(dc, name,
            shapes ? TerrainBrushShapeLabel(static_cast<kb::terrain_editor::TerrainBrushShape>(index))
                   : TerrainBrushLabel(static_cast<kb::terrain_editor::TerrainBrushMode>(index)),
            EditorDialogStyle::Color(theme.textPrimary), 12, selected ? FW_SEMIBOLD : FW_NORMAL);
        EditorDialogStyle::PaintText(dc, description,
            shapes ? TerrainBrushShapeDescription(static_cast<kb::terrain_editor::TerrainBrushShape>(index))
                   : TerrainBrushDescription(static_cast<kb::terrain_editor::TerrainBrushMode>(index)),
            EditorDialogStyle::Color(theme.textSecondary), 11);
    }
}

} // namespace kb::editor

#endif
