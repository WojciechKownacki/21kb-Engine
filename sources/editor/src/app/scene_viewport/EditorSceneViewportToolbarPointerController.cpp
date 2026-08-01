#include "app/scene_viewport/EditorSceneViewportToolbarPointerController.hpp"

#include "rendering/EditorPanelContentResolver.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"
#include "scene/EditorSceneContext.hpp"
#include "scene/EditorTerrainService.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] bool SelectDropdownValue(EditorViewportPreviewState& preview, const SceneViewportToolbarRects& toolbar, int x, int y) noexcept {
    const EditorViewportToolbarDropdown dropdown = preview.ToolbarDropdown();
    if (dropdown == EditorViewportToolbarDropdown::None) {
        return false;
    }

    std::size_t count = EditorViewportSnapStepOptionCount();
    if (dropdown == EditorViewportToolbarDropdown::GridSpacing) {
        count = EditorViewportGridSpacingOptionCount();
    } else if (dropdown == EditorViewportToolbarDropdown::RotationSnap) {
        count = EditorViewportRotationSnapOptionCount();
    }
    for (std::size_t index = 0; index < count && index < toolbar.dropdownItems.size(); ++index) {
        if (!PointInRect(toolbar.dropdownItems[index], x, y)) {
            continue;
        }
        if (dropdown == EditorViewportToolbarDropdown::GridSpacing) {
            preview.SetGridSpacing(EditorViewportGridSpacingOption(index));
        } else if (dropdown == EditorViewportToolbarDropdown::SnapStep) {
            preview.SetSnapStep(EditorViewportSnapStepOption(index));
        } else if (dropdown == EditorViewportToolbarDropdown::RotationSnap) {
            preview.SetRotationSnapDegrees(EditorViewportRotationSnapOption(index));
        }
        preview.CloseToolbarDropdown();
        return true;
    }
    return false;
}

[[nodiscard]] bool HandleTerrainTools(
    EditorSceneContext& sceneContext,
    const TerrainViewportToolbarRects& toolbar,
    int x,
    int y) {
    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();
    if (!sceneContext.IsProjectPluginEnabled("Editor.Terrain") ||
        !EditorTerrainService::IsTerrainEntity(sceneContext.Scene(), selected)) {
        return false;
    }
    EditorTerrainToolState& tool = EditorTerrainService::ToolState();
    const auto selectBrush = [&tool](std::size_t index) {
        tool.brush.mode = static_cast<kb::terrain_editor::TerrainBrushMode>(index);
        const bool holes =
            tool.brush.mode == kb::terrain_editor::TerrainBrushMode::CutHole ||
            tool.brush.mode == kb::terrain_editor::TerrainBrushMode::FillHole;
        tool.mode = holes ? EditorTerrainToolMode::Holes : EditorTerrainToolMode::Sculpt;
        tool.editingEnabled = true;
        tool.brushMenuOpen = false;
        tool.brushShapeMenuOpen = false;
    };
    if (PointInRect(toolbar.selectButton, x, y)) {
        tool.mode = EditorTerrainToolMode::Select;
        tool.editingEnabled = false;
        tool.strokeActive = false;
        tool.hoverVisible = false;
        tool.brushMenuOpen = false;
        tool.brushShapeMenuOpen = false;
        return true;
    }
    if (PointInRect(toolbar.sculptButton, x, y)) {
        tool.mode = EditorTerrainToolMode::Sculpt;
        tool.editingEnabled = true;
        if (tool.brush.mode == kb::terrain_editor::TerrainBrushMode::CutHole ||
            tool.brush.mode == kb::terrain_editor::TerrainBrushMode::FillHole) {
            tool.brush.mode = kb::terrain_editor::TerrainBrushMode::Raise;
        }
        tool.brushMenuOpen = false;
        tool.brushShapeMenuOpen = false;
        return true;
    }
    if (PointInRect(toolbar.holesButton, x, y)) {
        tool.mode = EditorTerrainToolMode::Holes;
        tool.editingEnabled = true;
        if (tool.brush.mode != kb::terrain_editor::TerrainBrushMode::CutHole &&
            tool.brush.mode != kb::terrain_editor::TerrainBrushMode::FillHole) {
            tool.brush.mode = kb::terrain_editor::TerrainBrushMode::CutHole;
        }
        tool.brushMenuOpen = false;
        tool.brushShapeMenuOpen = false;
        return true;
    }
    if (PointInRect(toolbar.brushButton, x, y)) {
        tool.brushMenuOpen = !tool.brushMenuOpen;
        tool.brushShapeMenuOpen = false;
        return true;
    }
    if (PointInRect(toolbar.brushShapeButton, x, y)) {
        tool.brushShapeMenuOpen = !tool.brushShapeMenuOpen;
        tool.brushMenuOpen = false;
        return true;
    }
    if (tool.brushMenuOpen) {
        for (std::size_t index = 0U; index < toolbar.brushItems.size(); ++index) {
            if (PointInRect(toolbar.brushItems[index], x, y)) {
                selectBrush(index);
                return true;
            }
        }
        if (PointInRect(toolbar.brushMenu, x, y)) return true;
        tool.brushMenuOpen = false;
        return true;
    }
    if (tool.brushShapeMenuOpen) {
        for (std::size_t index = 0U; index < toolbar.brushShapeItems.size(); ++index) {
            if (!PointInRect(toolbar.brushShapeItems[index], x, y)) continue;
            tool.brush.shape = static_cast<kb::terrain_editor::TerrainBrushShape>(index);
            tool.brushShapeMenuOpen = false;
            return true;
        }
        if (PointInRect(toolbar.brushShapeMenu, x, y)) return true;
        tool.brushShapeMenuOpen = false;
        return true;
    }
    if (PointInRect(toolbar.sizeMinusButton, x, y)) {
        tool.brush.radius = std::max(0.25F, tool.brush.radius * 0.8F);
        return true;
    }
    if (PointInRect(toolbar.sizePlusButton, x, y)) {
        tool.brush.radius = std::min(100'000.0F, tool.brush.radius * 1.25F);
        return true;
    }
    if (PointInRect(toolbar.strengthMinusButton, x, y)) {
        tool.brush.strength = std::max(0.0F, tool.brush.strength - 0.25F);
        return true;
    }
    if (PointInRect(toolbar.strengthPlusButton, x, y)) {
        tool.brush.strength = std::min(100'000.0F, tool.brush.strength + 0.25F);
        return true;
    }
    return PointInRect(toolbar.panel, x, y);
}

} // namespace

EditorSceneViewportToolbarPointerController::EditorSceneViewportToolbarPointerController(EditorSceneContext& sceneContext, EditorSceneBgfxViewport& sceneViewport) noexcept
    : sceneContext_(sceneContext)
    , sceneViewport_(sceneViewport) {}

bool EditorSceneViewportToolbarPointerController::HandlePointerDown(const EditorResolvedPanelContent& panelContent, int x, int y) {
    EditorViewportPreviewState& preview = sceneContext_.ViewportPreview(panelContent.panelId);
    const SceneViewportToolbarRects toolbar = SceneViewportToolbarRenderer::Resolve(panelContent.content, preview);
    if (HandleTerrainTools(
            sceneContext_, SceneViewportToolbarRenderer::ResolveTerrainTools(panelContent.content), x, y)) {
        preview.CloseToolbarDropdown();
        sceneViewport_.RequestPresent();
        return true;
    }
    if (SelectDropdownValue(preview, toolbar, x, y)) {
        sceneViewport_.RequestPresent();
        return true;
    }
    if (PointInRect(toolbar.renderProfileButton, x, y)) {
        preview.CloseToolbarDropdown();
        preview.CycleRenderProfile();
        sceneViewport_.RequestPresent();
        return true;
    }
    if (PointInRect(toolbar.gridToggleButton, x, y)) {
        preview.CloseToolbarDropdown();
        preview.ToggleGridVisible();
        sceneViewport_.RequestPresent();
        return true;
    }
    if (PointInRect(toolbar.gridStepButton, x, y)) {
        preview.ToggleToolbarDropdown(EditorViewportToolbarDropdown::GridSpacing);
        sceneViewport_.RequestPresent();
        return true;
    }
    if (PointInRect(toolbar.snapToggleButton, x, y)) {
        preview.CloseToolbarDropdown();
        preview.ToggleSnapEnabled();
        sceneViewport_.RequestPresent();
        return true;
    }
    if (PointInRect(toolbar.snapStepButton, x, y)) {
        preview.ToggleToolbarDropdown(EditorViewportToolbarDropdown::SnapStep);
        sceneViewport_.RequestPresent();
        return true;
    }
    if (PointInRect(toolbar.rotationSnapButton, x, y)) {
        preview.ToggleToolbarDropdown(EditorViewportToolbarDropdown::RotationSnap);
        sceneViewport_.RequestPresent();
        return true;
    }
    if (preview.ToolbarDropdown() != EditorViewportToolbarDropdown::None) {
        preview.CloseToolbarDropdown();
        sceneViewport_.RequestPresent();
        return true;
    }
    return false;
}

} // namespace kb::editor
