#include "rendering/HierarchyRowRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/HierarchyPanelStyle.hpp"
#include "rendering/HierarchyRowLayout.hpp"
#include "rendering/gdi/ScopedBrush.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <string>

namespace kb::editor {

namespace {

void DrawVisibilityCell(HDC dc, const HierarchyRowLayoutRects& layout, bool visible) {
    HeroIconPainter::Draw(dc, layout.visibilityIcon, HeroIconKind::Eye, visible ? HierarchyPanelStyle::MutedText() : HierarchyPanelStyle::RowTextHidden(), 1);
}

void DrawExpander(HDC dc, const RECT& rect, bool expanded) {
    const ScopedBrush brush(HierarchyPanelStyle::MutedText());
    const ScopedGdiObject selectedBrush(dc, brush.handle);
    const ScopedGdiObject selectedPen(dc, GetStockObject(NULL_PEN));

    POINT points[3]{};
    if (expanded) {
        points[0] = POINT{ rect.left, rect.top };
        points[1] = POINT{ rect.right, rect.top };
        points[2] = POINT{ (rect.left + rect.right) / 2, rect.bottom };
    } else {
        points[0] = POINT{ rect.left, rect.top };
        points[1] = POINT{ rect.left, rect.bottom };
        points[2] = POINT{ rect.right, (rect.top + rect.bottom) / 2 };
    }
    Polygon(dc, points, 3);
}

} // namespace

void HierarchyRowRenderer::Paint(HDC dc, const RECT& rowRect, const EditorTheme&, const EditorHierarchyRow& row, bool selected) const {
    if (selected) {
        GdiDrawing::FillRectColor(dc, rowRect, HierarchyPanelStyle::RowSelected());
    }

    ScopedFont font{ 12, FW_NORMAL };
    const ScopedGdiObject selectedFont(dc, font.handle);

    const HierarchyRowLayoutRects layout = HierarchyRowLayout::Resolve(rowRect, row);
    DrawVisibilityCell(dc, layout, row.visible);

    if (row.hasChildren) {
        DrawExpander(dc, layout.expanderIcon, row.expanded);
    }

    const COLORREF rowInk = row.visible ? (selected ? HierarchyPanelStyle::RowTextSelected() : HierarchyPanelStyle::RowText()) : HierarchyPanelStyle::RowTextHidden();
    const COLORREF entityIcon = row.prefabRoot ? HierarchyPanelStyle::PrefabCubeStroke() : HierarchyPanelStyle::CubeStroke();
    HeroIconPainter::Draw(dc, layout.entityIcon, HeroIconKind::Cube, row.visible ? entityIcon : HierarchyPanelStyle::RowTextHidden(), 2);
    GdiDrawing::DrawTextBlock(dc, layout.label, row.name.c_str(), rowInk);
}

} // namespace kb::editor

#endif
