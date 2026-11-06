#include "rendering/scene_viewport_toolbar/TerrainToolbarDrawer.hpp"

#if defined(_WIN32)
#include "localization/EditorLocalization.hpp"
#include "platform/win32/Utf8Gdi.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarDrawing.hpp"
#include "rendering/scene_viewport_toolbar/TerrainToolbarLabels.hpp"
#include "rendering/scene_viewport_toolbar/TerrainToolbarMetrics.hpp"
#include "scene/EditorTerrainJobs.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace kb::editor {
namespace {

using namespace terrain_toolbar;

void DrawLabel(HDC dc, RECT rect, std::string_view text, COLORREF color) {
    ScopedFont font(12, FW_NORMAL);
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    kb::editor::DrawTextUtf8(dc, text.data(), static_cast<int>(text.size()), &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

[[nodiscard]] std::string_view JobName(EditorTerrainJobKind kind) {
    switch (kind) {
    case EditorTerrainJobKind::World: return EditorText("terrainTool.job.world", "Generating world");
    case EditorTerrainJobKind::Thermal: return EditorText("terrainTool.job.thermal", "Thermal erosion");
    case EditorTerrainJobKind::Hydraulic: return EditorText("terrainTool.job.hydraulic", "Hydraulic erosion");
    case EditorTerrainJobKind::Settings: return EditorText("terrainTool.job.settings", "Applying settings");
    case EditorTerrainJobKind::None: break;
    }
    return {};
}

void PaintJob(HDC dc, const TerrainViewportToolbarRects& rects, const EditorTheme& theme, EditorTerrainJobKind job) {
    SceneViewportToolbarDrawing::FillRound(dc, rects.jobBar, RGB(22, 26, 32), RGB(52, 62, 74), 6);
    const float progress = EditorTerrainJobs::Progress();
    RECT filled = rects.jobBar;
    filled.left += 1;
    filled.top += 1;
    filled.bottom -= 1;
    filled.right = filled.left + static_cast<LONG>(static_cast<float>(rects.jobBar.right - rects.jobBar.left - 2) * progress);
    GdiDrawing::FillRectColor(dc, filled, RGB(36, 91, 111));
    const std::string text = EditorLocalization::Format("terrainTool.job.progress", "{job} {percent}%",
        { { "job", std::string{ JobName(job) } }, { "percent", std::to_string(static_cast<int>(progress * 100.0F)) } });
    GdiDrawing::DrawCenteredText(dc, rects.jobBar, text, GdiDrawing::ToColorRef(theme.textPrimary));
    SceneViewportToolbarDrawing::DrawChip(dc, rects.cancelButton, EditorText("terrainTool.cancel", "Cancel"), theme, false);
}

// The splat map size on offer, the current one lit.
void PaintSplat(HDC dc, const TerrainViewportToolbarRects& rects, const EditorTheme& theme, std::uint32_t splatResolution) {
    DrawLabel(dc, Box(rects.drawer.left + kPad, rects.splatButtons[0].top, kLabelWidth - 6), EditorText("terrainTool.splatMap", "Splat map"),
        GdiDrawing::ToColorRef(theme.textSecondary));
    for (std::size_t index = 0U; index < rects.splatButtons.size(); ++index) {
        SceneViewportToolbarDrawing::DrawChip(dc, rects.splatButtons[index], std::to_string(kEditorTerrainSplatResolutions[index]), theme,
            splatResolution == kEditorTerrainSplatResolutions[index]);
    }
}

void PaintWizard(HDC dc, const TerrainViewportToolbarRects& rects, const EditorTheme& theme, const EditorTerrainToolState& tool, EditorTerrainJobKind job,
    const TerrainDrawerValues& values) {
    const COLORREF secondary = GdiDrawing::ToColorRef(theme.textSecondary);
    const RECT titleRow = Box(rects.drawer.left + kPad, rects.drawer.top + kPad, kLabelWidth - 6);
    DrawLabel(dc, titleRow, EditorText("terrainTool.newWorld.title", "New world"), GdiDrawing::ToColorRef(theme.textPrimary));
    const std::array<std::string_view, 4U> names{
        EditorText("terrainTool.world.island", "Island"),
        EditorText("terrainTool.world.mountains", "Mountains"),
        EditorText("terrainTool.world.plains", "Plains"),
        EditorText("terrainTool.world.valley", "Valley"),
    };
    for (std::size_t index = 0U; index < names.size(); ++index) {
        SceneViewportToolbarDrawing::DrawChip(dc, rects.worldPresetButtons[index], names[index], theme, tool.worldPreset == index);
    }
    const RECT characterLabel = Box(rects.drawer.left + kPad, rects.characterTrack.top, kLabelWidth - 6);
    DrawLabel(dc, characterLabel, EditorText("terrainTool.newWorld.character", "Character"), secondary);
    // The track: gentle on the left, dramatic on the right.
    const int middle = (rects.characterTrack.top + rects.characterTrack.bottom) / 2;
    const RECT groove{ rects.characterTrack.left, middle - 3, rects.characterTrack.right, middle + 3 };
    SceneViewportToolbarDrawing::FillRound(dc, groove, RGB(22, 26, 32), RGB(52, 62, 74), 4);
    const int knobX = rects.characterTrack.left + static_cast<int>(tool.worldCharacter * static_cast<float>(rects.characterTrack.right - rects.characterTrack.left));
    RECT filledGroove = groove;
    filledGroove.right = knobX;
    GdiDrawing::FillRectColor(dc, RECT{ filledGroove.left + 1, filledGroove.top + 1, filledGroove.right, filledGroove.bottom - 1 }, RGB(36, 91, 111));
    SceneViewportToolbarDrawing::FillRound(dc, RECT{ knobX - 6, middle - 9, knobX + 6, middle + 9 }, RGB(120, 205, 230), RGB(235, 250, 255), 6);
    SceneViewportToolbarDrawing::DrawValueBox(dc, rects.characterValue, TerrainToolbarLabels::FormatNumber(tool.worldCharacter), theme);
    if (job != EditorTerrainJobKind::None) {
        PaintJob(dc, rects, theme, job);
    } else {
        SceneViewportToolbarDrawing::DrawChip(dc, rects.generateButton, EditorText("terrainTool.newWorld.generate", "Generate"), theme, true);
    }
    PaintSplat(dc, rects, theme, values.splatResolution);
}

void PaintAdvanced(HDC dc, const TerrainViewportToolbarRects& rects, const EditorTheme& theme, const EditorTerrainToolState& tool, EditorTerrainJobKind job,
    const TerrainDrawerValues& values) {
    const COLORREF secondary = GdiDrawing::ToColorRef(theme.textSecondary);
    SceneViewportToolbarDrawing::DrawValueButton(dc, rects.operationButton, HeroIconKind::AdjustmentsHorizontal,
        TerrainToolbarLabels::BrushToolbarLabel(tool.brush.mode), theme, tool.brushMenuOpen);
    SceneViewportToolbarDrawing::DrawValueButton(dc, rects.brushShapeButton, HeroIconKind::RectangleGroup,
        TerrainToolbarLabels::BrushShapeLabel(tool.brush.shape), theme, tool.brushShapeMenuOpen);
    const auto stepper = [&](const TerrainStepperRects& rect, std::string_view key, std::string_view english, const std::string& value) {
        SceneViewportToolbarDrawing::DrawStepper(dc, rect.minus, rect.value,
            rect.plus, EditorLocalization::Format(key, english, { { "value", value } }), theme);
    };
    stepper(rects.falloff, "terrainTool.falloff", "Falloff {value}", TerrainToolbarLabels::FormatNumber(tool.brush.falloff));
    stepper(rects.terraceStep, "terrainTool.terraceStep", "Terrace {value}", TerrainToolbarLabels::FormatNumber(tool.brush.terraceStep));
    stepper(rects.seed, "terrainTool.seed", "Seed {value}", std::to_string(tool.brush.noiseSeed));

    SceneViewportToolbarDrawing::DrawChip(dc, rects.symmetryXButton, EditorText("terrainTool.symmetryX", "Mirror X"), theme, tool.brush.symmetryX);
    SceneViewportToolbarDrawing::DrawChip(dc, rects.symmetryZButton, EditorText("terrainTool.symmetryZ", "Mirror Z"), theme, tool.brush.symmetryZ);
    DrawLabel(dc, Box(rects.resolutionButtons[0].left - kLabelWidth + 12, rects.resolutionButtons[0].top, kLabelWidth - 16),
        EditorText("terrainTool.resolution", "Resolution"), secondary);
    for (std::size_t index = 0U; index < kResolutions.size(); ++index) {
        SceneViewportToolbarDrawing::DrawChip(dc, rects.resolutionButtons[index], std::to_string(kResolutions[index]), theme,
            values.resolution == kResolutions[index]);
    }
    const std::string image = tool.stampImageName.empty()
        ? std::string{ EditorText("terrainTool.stampImage.pick", "Stamp image...") }
        : EditorLocalization::Format("terrainTool.stampImage.named", "Image: {name}", { { "name", tool.stampImageName } });
    SceneViewportToolbarDrawing::DrawChip(dc, rects.stampImageButton, image, theme, !tool.stampImageName.empty());

    PaintSplat(dc, rects, theme, values.splatResolution);
    // These two are written with their texts in the call so the translation check finds them.
    SceneViewportToolbarDrawing::DrawStepper(dc, rects.lodError.minus, rects.lodError.value, rects.lodError.plus,
        EditorLocalization::Format("terrainTool.lodError", "LOD error {value}", { { "value", TerrainToolbarLabels::FormatNumber(values.lodPixelError) } }), theme);
    SceneViewportToolbarDrawing::DrawStepper(dc, rects.collisionStep.minus, rects.collisionStep.value, rects.collisionStep.plus,
        EditorLocalization::Format("terrainTool.collisionStep", "Collision step {value}", { { "value", std::to_string(values.collisionStep) } }), theme);

    SceneViewportToolbarDrawing::DrawChip(dc, rects.thermalButton, EditorText("terrainTool.erode.thermal", "Thermal erosion"), theme, job == EditorTerrainJobKind::Thermal);
    SceneViewportToolbarDrawing::DrawChip(dc, rects.hydraulicButton, EditorText("terrainTool.erode.hydraulic", "Hydraulic erosion"), theme, job == EditorTerrainJobKind::Hydraulic);
    if (job != EditorTerrainJobKind::None) PaintJob(dc, rects, theme, job);
}

} // namespace

void TerrainToolbarDrawer::Paint(
    HDC dc, const TerrainViewportToolbarRects& rects, const EditorTheme& theme, const EditorTerrainToolState& tool, const TerrainDrawerValues& values) {
    if (tool.drawer == EditorTerrainDrawer::None || rects.drawer.right <= rects.drawer.left) return;
    SceneViewportToolbarDrawing::FillRound(dc, rects.drawer, RGB(20, 24, 30), RGB(67, 78, 92), 9);
    const EditorTerrainJobKind job = EditorTerrainJobs::Active();
    if (tool.drawer == EditorTerrainDrawer::NewWorld) PaintWizard(dc, rects, theme, tool, job, values);
    else PaintAdvanced(dc, rects, theme, tool, job, values);
}

} // namespace kb::editor

#endif
