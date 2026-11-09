#include "rendering/scene_viewport_toolbar/TerrainToolbarDrawer.hpp"
#include "rendering/scene_viewport_toolbar/TerrainToolbarMetrics.hpp"

#if defined(_WIN32)

namespace kb::editor {

using namespace terrain_toolbar;

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

} // namespace kb::editor

#endif
