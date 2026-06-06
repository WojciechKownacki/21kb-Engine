#include "rendering/ProjectFilesAssetTileFrameRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/ProjectFilesPanelDrawing.hpp"

namespace kb::editor {
namespace {

using Draw = ProjectFilesPanelDrawing;

[[nodiscard]] COLORREF TileFill(const EditorTheme& theme, bool selected) {
    return selected ? Draw::Blend(Draw::Color(theme.panel), RGB(96, 108, 126), 28) : Draw::Blend(Draw::Color(theme.panel), RGB(52, 58, 68), 16);
}

void DrawAlphaBorderRing(HDC dc, RECT rect, COLORREF color, BYTE alpha) {
    RECT top{ rect.left, rect.top, rect.right, rect.top + 1 };
    RECT left{ rect.left, rect.top + 1, rect.left + 1, rect.bottom - 1 };
    RECT bottom{ rect.left, rect.bottom - 1, rect.right, rect.bottom };
    RECT right{ rect.right - 1, rect.top + 1, rect.right, rect.bottom - 1 };
    GdiDrawing::FillRectAlpha(dc, top, color, alpha);
    GdiDrawing::FillRectAlpha(dc, left, color, alpha);
    GdiDrawing::FillRectAlpha(dc, bottom, color, alpha);
    GdiDrawing::FillRectAlpha(dc, right, color, alpha);
}

void DrawTileAlphaFrame(HDC dc, RECT tile, const EditorTheme& theme, bool selected, bool focused) {
    const COLORREF color = selected ? RGB(126, 138, 156) : Draw::Blend(Draw::Color(theme.borderPanel), RGB(106, 116, 132), 14);
    const BYTE selectedAlpha[5]{ 150, 66, 24, 0, 0 };
    const BYTE inactiveAlpha[5]{ 82, 34, 12, 0, 0 };
    const BYTE normalAlpha[5]{ 34, 14, 0, 0, 0 };
    const BYTE* alphas = selected ? (focused ? selectedAlpha : inactiveAlpha) : normalAlpha;

    RECT ring = tile;
    for (int index = 0; index < 5; ++index) {
        DrawAlphaBorderRing(dc, ring, color, alphas[index]);
        InflateRect(&ring, -1, -1);
    }
}

} // namespace

void ProjectFilesAssetTileFrameRenderer::Paint(HDC dc, RECT tile, const EditorTheme& theme, bool selected, bool focused) {
    GdiDrawing::FillRectColor(dc, tile, TileFill(theme, selected));
    DrawTileAlphaFrame(dc, tile, theme, selected, focused);
}

} // namespace kb::editor

#endif
