#include "app/scene_viewport/EditorTerrainToolbarPointer.hpp"

#if defined(_WIN32)
#include "app/scene_viewport/EditorTerrainFileDialog.hpp"
#include "console/EditorConsoleState.hpp"
#include "localization/EditorLocalization.hpp"
#include "scene/EditorSceneContext.hpp"
#include "scene/EditorTerrainJobs.hpp"
#include "scene/EditorTerrainService.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace kb::editor {
namespace {

constexpr std::array<std::uint32_t, 5U> kResolutions{ 65U, 129U, 257U, 513U, 1025U };

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

void Warn(EditorSceneContext& sceneContext, const std::string& error, const char* fallback) {
    sceneContext.Console().Warning("Terrain", error.empty() ? fallback : error);
}

// A click on a tool of the bar: it becomes the active tool (the active one is switched off). A Stamp with no
// image yet asks for one first.
void ClickTool(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, EditorTerrainTool selected) {
    EditorTerrainToolState& tool = EditorTerrainService::ToolState();
    std::string error;
    if (selected == EditorTerrainTool::Stamp && tool.brush.stampImage == nullptr && EditorTerrainService::ActiveTool(tool) != selected) {
        const std::optional<std::filesystem::path> path = EditorTerrainFileDialog::PickHeightImage(
            GetActiveWindow(), EditorText("fileDialog.stampImageTitle", "Pick a Stamp Height Image"));
        if (!path.has_value()) return;
        if (!EditorTerrainService::LoadStampImage(tool, *path, &error)) {
            Warn(sceneContext, error, "The stamp image could not be loaded.");
            return;
        }
    }
    if (!EditorTerrainService::SelectTool(tool, selected, sceneContext.TerrainForEditing(entity), &error)) {
        Warn(sceneContext, error, "This terrain tool is not ready yet.");
    }
}

void ClickDrawer(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const TerrainViewportToolbarRects& bar, int x, int y) {
    EditorTerrainToolState& tool = EditorTerrainService::ToolState();
    std::string error;
    const auto start = [&](bool started) {
        if (!started) Warn(sceneContext, error, "The terrain job could not be started.");
    };
    if (tool.drawer == EditorTerrainDrawer::NewWorld) {
        for (std::size_t index = 0U; index < bar.worldPresetButtons.size(); ++index) {
            if (PointInRect(bar.worldPresetButtons[index], x, y)) tool.worldPreset = static_cast<std::uint8_t>(index);
        }
        if (PointInRect(bar.characterTrack, x, y)) {
            const float span = static_cast<float>(bar.characterTrack.right - bar.characterTrack.left);
            tool.worldCharacter = std::clamp(static_cast<float>(x - bar.characterTrack.left) / span, 0.0F, 1.0F);
        }
        if (EditorTerrainJobs::Active() == EditorTerrainJobKind::None) {
            if (PointInRect(bar.generateButton, x, y)) {
                start(EditorTerrainJobs::StartWorld(
                    sceneContext, entity, kEditorTerrainWorldPresets[tool.worldPreset], tool.brush.noiseSeed, tool.worldCharacter, &error));
            }
        } else if (PointInRect(bar.cancelButton, x, y)) {
            EditorTerrainJobs::Cancel();
        }
        return;
    }
    kb::terrain_editor::TerrainBrushSettings& brush = tool.brush;
    const auto step = [&](const TerrainStepperRects& rects, float& value, float delta, float low, float high) {
        if (PointInRect(rects.minus, x, y)) value = std::clamp(value - delta, low, high);
        if (PointInRect(rects.plus, x, y)) value = std::clamp(value + delta, low, high);
    };
    step(bar.falloff, brush.falloff, 0.05F, 0.0F, 1.0F);
    step(bar.terraceStep, brush.terraceStep, 0.5F, 0.1F, 100'000.0F);
    if (PointInRect(bar.seed.minus, x, y)) brush.noiseSeed = brush.noiseSeed > 0U ? brush.noiseSeed - 1U : 0U;
    if (PointInRect(bar.seed.plus, x, y)) ++brush.noiseSeed;
    if (PointInRect(bar.operationButton, x, y)) {
        tool.brushMenuOpen = !tool.brushMenuOpen;
        tool.brushShapeMenuOpen = false;
    }
    if (PointInRect(bar.brushShapeButton, x, y)) {
        tool.brushShapeMenuOpen = !tool.brushShapeMenuOpen;
        tool.brushMenuOpen = false;
    }
    if (PointInRect(bar.symmetryXButton, x, y)) brush.symmetryX = !brush.symmetryX;
    if (PointInRect(bar.symmetryZButton, x, y)) brush.symmetryZ = !brush.symmetryZ;
    if (PointInRect(bar.stampImageButton, x, y)) {
        if (const std::optional<std::filesystem::path> path = EditorTerrainFileDialog::PickHeightImage(
                GetActiveWindow(), EditorText("fileDialog.stampImageTitle", "Pick a Stamp Height Image"));
            path.has_value() && !EditorTerrainService::LoadStampImage(tool, *path, &error)) {
            Warn(sceneContext, error, "The stamp image could not be loaded.");
        }
    }
    for (std::size_t index = 0U; index < kResolutions.size(); ++index) {
        if (!PointInRect(bar.resolutionButtons[index], x, y)) continue;
        const kb::assets::TerrainAsset* terrain = sceneContext.TerrainForEditing(entity);
        if (terrain == nullptr) break;
        if (!sceneContext.ConfigureTerrain(entity, EditorTerrainConfiguration{
                .width = kResolutions[index], .height = kResolutions[index], .chunkQuads = terrain->chunkQuads,
                .lodCount = terrain->lodCount, .worldSizeX = terrain->worldSizeX, .worldSizeZ = terrain->worldSizeZ }, &error)) {
            Warn(sceneContext, error, "The terrain could not be reconfigured.");
        }
    }
    if (EditorTerrainJobs::Active() == EditorTerrainJobKind::None) {
        if (PointInRect(bar.thermalButton, x, y)) start(EditorTerrainJobs::StartErosion(sceneContext, entity, EditorTerrainJobKind::Thermal, &error));
        if (PointInRect(bar.hydraulicButton, x, y)) start(EditorTerrainJobs::StartErosion(sceneContext, entity, EditorTerrainJobKind::Hydraulic, &error));
    } else if (PointInRect(bar.cancelButton, x, y)) {
        EditorTerrainJobs::Cancel();
    }
}

} // namespace

bool EditorTerrainToolbarPointer::HandleClick(EditorSceneContext& sceneContext, const TerrainViewportToolbarRects& bar, int x, int y) {
    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();
    if (!sceneContext.IsProjectPluginEnabled("Editor.Terrain") || !EditorTerrainService::IsTerrainEntity(sceneContext.Scene(), selected)) {
        return false;
    }
    EditorTerrainToolState& tool = EditorTerrainService::ToolState();
    using Tool = EditorTerrainTool;
    const std::array<std::pair<const RECT*, Tool>, 5U> tools{ {
        { &bar.sculptButton, Tool::Sculpt }, { &bar.smoothButton, Tool::Smooth }, { &bar.paintButton, Tool::Paint },
        { &bar.rampButton, Tool::Ramp }, { &bar.stampButton, Tool::Stamp } } };
    for (const auto& [rect, which] : tools) {
        if (PointInRect(*rect, x, y)) {
            ClickTool(sceneContext, selected, which);
            return true;
        }
    }
    const auto toggleDrawer = [&tool](EditorTerrainDrawer drawer) {
        tool.drawer = tool.drawer == drawer ? EditorTerrainDrawer::None : drawer;
        tool.brushMenuOpen = false;
        tool.brushShapeMenuOpen = false;
    };
    if (PointInRect(bar.worldButton, x, y)) {
        toggleDrawer(EditorTerrainDrawer::NewWorld);
        return true;
    }
    if (PointInRect(bar.advancedButton, x, y)) {
        toggleDrawer(EditorTerrainDrawer::Advanced);
        return true;
    }
    if (PointInRect(bar.slopeButton, x, y)) {
        tool.slopeOverlay = !tool.slopeOverlay;
        return true;
    }
    if (PointInRect(bar.variantButton, x, y)) {
        const kb::assets::TerrainAsset* terrain = sceneContext.TerrainForEditing(selected);
        EditorTerrainService::CycleToolVariant(tool, terrain != nullptr ? terrain->materialLayers.size() : 0U);
        return true;
    }
    if (PointInRect(bar.size.minus, x, y)) {
        tool.brush.radius = std::max(0.25F, tool.brush.radius * 0.8F);
        return true;
    }
    if (PointInRect(bar.size.plus, x, y)) {
        tool.brush.radius = std::min(100'000.0F, tool.brush.radius * 1.25F);
        return true;
    }
    if (PointInRect(bar.strength.minus, x, y)) {
        tool.brush.strength = std::max(0.0F, tool.brush.strength - 0.25F);
        return true;
    }
    if (PointInRect(bar.strength.plus, x, y)) {
        const float maximum = tool.mode == EditorTerrainToolMode::Paint ? 1.0F : 100'000.0F;
        tool.brush.strength = std::min(maximum, tool.brush.strength + 0.25F);
        return true;
    }
    if (PointInRect(bar.drawer, x, y)) {
        ClickDrawer(sceneContext, selected, bar, x, y);
        return true;
    }
    if (tool.brushMenuOpen) {
        for (std::size_t index = 0U; index < bar.brushItems.size(); ++index) {
            if (!PointInRect(bar.brushItems[index], x, y)) continue;
            tool.brush.mode = static_cast<kb::terrain_editor::TerrainBrushMode>(index);
            const bool holes = tool.brush.mode == kb::terrain_editor::TerrainBrushMode::CutHole ||
                tool.brush.mode == kb::terrain_editor::TerrainBrushMode::FillHole;
            tool.mode = holes ? EditorTerrainToolMode::Holes : EditorTerrainToolMode::Sculpt;
            tool.editingEnabled = true;
            tool.brushMenuOpen = false;
            return true;
        }
        if (!PointInRect(bar.brushMenu, x, y)) tool.brushMenuOpen = false;
        return true;
    }
    if (tool.brushShapeMenuOpen) {
        for (std::size_t index = 0U; index < bar.brushShapeItems.size(); ++index) {
            if (!PointInRect(bar.brushShapeItems[index], x, y)) continue;
            tool.brush.shape = static_cast<kb::terrain_editor::TerrainBrushShape>(index);
            tool.brushShapeMenuOpen = false;
            return true;
        }
        if (!PointInRect(bar.brushShapeMenu, x, y)) tool.brushShapeMenuOpen = false;
        return true;
    }
    return PointInRect(bar.panel, x, y);
}

} // namespace kb::editor

#endif
