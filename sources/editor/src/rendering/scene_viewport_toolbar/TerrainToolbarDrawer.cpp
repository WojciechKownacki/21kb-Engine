#include "rendering/scene_viewport_toolbar/TerrainToolbarDrawer.hpp"

#if defined(_WIN32)
#include "localization/EditorLocalization.hpp"
#include "platform/win32/Utf8Gdi.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarDrawing.hpp"
#include "rendering/scene_viewport_toolbar/TerrainToolbarLabels.hpp"
#include "scene/EditorTerrainJobs.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace kb::editor {
namespace {

constexpr int kPad = 10;
constexpr int kRowHeight = 26;
constexpr int kRowGap = 6;
constexpr int kChipGap = 4;
constexpr int kLabelWidth = 92;
constexpr std::array<std::uint32_t, 5U> kResolutions{ 65U, 129U, 257U, 513U, 1025U };

[[nodiscard]] RECT Box(int left, int top, int width) noexcept {
    return RECT{ left, top, left + width, top + kRowHeight };
}

[[nodiscard]] TerrainStepperRects Stepper(int& cursor, int top, int valueWidth) noexcept {
    TerrainStepperRects stepper;
    stepper.minus = Box(cursor, top, 24);
    stepper.value = Box(stepper.minus.right + kChipGap, top, valueWidth);
    stepper.plus = Box(stepper.value.right + kChipGap, top, 24);
    cursor = stepper.plus.right + 10;
    return stepper;
}

// The running job's bar and its Cancel button, from `left` on row `top`.
void LayoutJob(TerrainViewportToolbarRects& rects, int left, int top) noexcept {
    rects.jobBar = Box(left, top, 230);
    rects.cancelButton = Box(rects.jobBar.right + kChipGap, top, 78);
}

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

void PaintWizard(HDC dc, const TerrainViewportToolbarRects& rects, const EditorTheme& theme, const EditorTerrainToolState& tool, EditorTerrainJobKind job) {
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
}

void PaintAdvanced(HDC dc, const TerrainViewportToolbarRects& rects, const EditorTheme& theme, const EditorTerrainToolState& tool, EditorTerrainJobKind job,
    std::uint32_t terrainResolution) {
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
            terrainResolution == kResolutions[index]);
    }
    const std::string image = tool.stampImageName.empty()
        ? std::string{ EditorText("terrainTool.stampImage.pick", "Stamp image...") }
        : EditorLocalization::Format("terrainTool.stampImage.named", "Image: {name}", { { "name", tool.stampImageName } });
    SceneViewportToolbarDrawing::DrawChip(dc, rects.stampImageButton, image, theme, !tool.stampImageName.empty());

    SceneViewportToolbarDrawing::DrawChip(dc, rects.thermalButton, EditorText("terrainTool.erode.thermal", "Thermal erosion"), theme, job == EditorTerrainJobKind::Thermal);
    SceneViewportToolbarDrawing::DrawChip(dc, rects.hydraulicButton, EditorText("terrainTool.erode.hydraulic", "Hydraulic erosion"), theme, job == EditorTerrainJobKind::Hydraulic);
    if (job != EditorTerrainJobKind::None) PaintJob(dc, rects, theme, job);
}

} // namespace

int TerrainToolbarDrawer::Layout(TerrainViewportToolbarRects& rects, EditorTerrainDrawer drawer) noexcept {
    const bool wizard = drawer == EditorTerrainDrawer::NewWorld;
    const int rows = wizard ? 2 : 3;
    rects.drawer = RECT{ rects.panel.left, rects.panel.bottom + 4, rects.panel.right,
        rects.panel.bottom + 4 + 2 * kPad + rows * kRowHeight + (rows - 1) * kRowGap };
    const int left = rects.drawer.left + kPad;
    const int row1 = rects.drawer.top + kPad;
    const int row2 = row1 + kRowHeight + kRowGap;
    const int row3 = row2 + kRowHeight + kRowGap;
    if (wizard) {
        int cursor = left + kLabelWidth;
        for (RECT& chip : rects.worldPresetButtons) {
            chip = Box(cursor, row1, 104);
            cursor = chip.right + kChipGap;
        }
        rects.characterTrack = Box(left + kLabelWidth, row2, 240);
        rects.characterValue = Box(rects.characterTrack.right + 10, row2, 48);
        const int actionLeft = rects.characterValue.right + 18;
        rects.generateButton = Box(actionLeft, row2, 120);
        LayoutJob(rects, actionLeft, row2);
        return rects.drawer.bottom;
    }
    int cursor = left;
    rects.operationButton = Box(cursor, row1, 170);
    rects.brushShapeButton = Box(rects.operationButton.right + kChipGap, row1, 150);
    cursor = rects.brushShapeButton.right + 14;
    rects.falloff = Stepper(cursor, row1, 116);
    rects.terraceStep = Stepper(cursor, row1, 100);
    rects.seed = Stepper(cursor, row1, 96);

    rects.symmetryXButton = Box(left, row2, 90);
    rects.symmetryZButton = Box(rects.symmetryXButton.right + kChipGap, row2, 90);
    cursor = rects.symmetryZButton.right + 14 + kLabelWidth - 12;
    for (RECT& chip : rects.resolutionButtons) {
        chip = Box(cursor, row2, 54);
        cursor = chip.right + kChipGap;
    }
    rects.stampImageButton = Box(cursor + 10, row2, 200);

    rects.thermalButton = Box(left, row3, 150);
    rects.hydraulicButton = Box(rects.thermalButton.right + kChipGap, row3, 160);
    LayoutJob(rects, rects.hydraulicButton.right + 14, row3);
    return rects.drawer.bottom;
}

void TerrainToolbarDrawer::Paint(
    HDC dc, const TerrainViewportToolbarRects& rects, const EditorTheme& theme, const EditorTerrainToolState& tool, std::uint32_t terrainResolution) {
    if (tool.drawer == EditorTerrainDrawer::None || rects.drawer.right <= rects.drawer.left) return;
    SceneViewportToolbarDrawing::FillRound(dc, rects.drawer, RGB(20, 24, 30), RGB(67, 78, 92), 9);
    const EditorTerrainJobKind job = EditorTerrainJobs::Active();
    if (tool.drawer == EditorTerrainDrawer::NewWorld) PaintWizard(dc, rects, theme, tool, job);
    else PaintAdvanced(dc, rects, theme, tool, job, terrainResolution);
}

} // namespace kb::editor

#endif
