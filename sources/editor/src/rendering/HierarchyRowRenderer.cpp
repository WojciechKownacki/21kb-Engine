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

[[nodiscard]] RECT Shrink(RECT rect, int left, int top, int right, int bottom) noexcept {
    rect.left += left;
    rect.top += top;
    rect.right -= right;
    rect.bottom -= bottom;
    return rect;
}

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

void DrawRenameField(HDC dc, RECT labelRect, std::string_view value, bool selectingAll) {
    RECT field = labelRect;
    field.top -= 2;
    field.bottom -= 2;
    GdiDrawing::DrawSharpFrame(dc, field, RGB(18, 20, 24), RGB(78, 86, 98));

    RECT textRect = Shrink(field, 6, 0, 4, 0);
    if (selectingAll && !value.empty()) {
        SIZE textSize{};
        const int textWidth = GetTextExtentPoint32A(dc, value.data(), static_cast<int>(value.size()), &textSize) ? textSize.cx : 0;
        RECT selection = textRect;
        selection.right = std::min(selection.right, selection.left + textWidth + 2);
        selection.top += 2;
        selection.bottom -= 2;
        GdiDrawing::FillRectColor(dc, selection, RGB(48, 76, 122));
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, HierarchyPanelStyle::RowTextSelected());
    DrawTextA(dc, value.data(), static_cast<int>(value.size()), &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

} // namespace

void HierarchyRowRenderer::Paint(HDC dc, const RECT& rowRect, const EditorTheme&, const EditorHierarchyRow& row, const EditorSceneContext& sceneContext) const {
    const bool selected = sceneContext.IsHierarchyEntitySelected(row.entity);
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
    HeroIconPainter::Draw(
        dc,
        layout.entityIcon,
        row.hasCamera ? HeroIconKind::Camera : HeroIconKind::Cube,
        row.visible ? entityIcon : HierarchyPanelStyle::RowTextHidden(),
        2);
    if (sceneContext.IsHierarchyRenaming(row.entity)) {
        DrawRenameField(dc, layout.label, sceneContext.HierarchyRenameBuffer(), sceneContext.IsHierarchyRenameSelectingAll());
    } else {
        GdiDrawing::DrawTextBlock(dc, layout.label, row.name.c_str(), rowInk);
    }
}

} // namespace kb::editor

#endif
