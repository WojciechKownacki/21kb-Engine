#include "rendering/HierarchyRowRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/HierarchyRowLayout.hpp"
#include "rendering/components/DenseListRow.hpp"
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

void DrawVisibilityCell(HDC dc, const HierarchyRowLayoutRects& layout, const EditorTheme& theme, bool visible) {
    HeroIconPainter::Draw(dc, layout.visibilityIcon, HeroIconKind::Eye, GdiDrawing::ToColorRef(visible ? theme.textSecondary : theme.textDisabled), 1);
}

void DrawExpander(HDC dc, const RECT& rect, const EditorTheme& theme, bool expanded) {
    const ScopedBrush brush(GdiDrawing::ToColorRef(theme.textSecondary));
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

void DrawRenameField(HDC dc, RECT labelRect, const EditorTheme& theme, std::string_view value, bool selectingAll) {
    RECT field = labelRect;
    field.top -= 2;
    field.bottom -= 2;
    GdiDrawing::DrawSharpFrame(dc, field, GdiDrawing::ToColorRef(theme.chrome), GdiDrawing::ToColorRef(theme.accent));

    RECT textRect = Shrink(field, 6, 0, 4, 0);
    if (selectingAll && !value.empty()) {
        SIZE textSize{};
        const int textWidth = GetTextExtentPoint32A(dc, value.data(), static_cast<int>(value.size()), &textSize) ? textSize.cx : 0;
        RECT selection = textRect;
        selection.right = std::min(selection.right, selection.left + textWidth + 2);
        selection.top += 2;
        selection.bottom -= 2;
        GdiDrawing::FillRectColor(dc, selection, GdiDrawing::ToColorRef(theme.tabActive));
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GdiDrawing::ToColorRef(theme.textPrimary));
    DrawTextA(dc, value.data(), static_cast<int>(value.size()), &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

} // namespace

void HierarchyRowRenderer::Paint(HDC dc, const RECT& rowRect, const EditorTheme& theme, const EditorHierarchyRow& row, const EditorSceneContext& sceneContext) const {
    const bool selected = sceneContext.IsHierarchyEntitySelected(row.entity);
    const HierarchyRowLayoutRects layout = HierarchyRowLayout::Resolve(rowRect, row);
    DenseListRow::Paint(dc, theme, DenseListRowDescriptor{
        .bounds = rowRect,
        .title = sceneContext.IsHierarchyRenaming(row.entity) ? std::string_view{} : std::string_view{row.name},
        .contentLeftInset = static_cast<int>(layout.label.left - rowRect.left),
        .contentRightInset = static_cast<int>(rowRect.right - layout.label.right),
        .selected = selected,
        .enabled = row.visible,
        .showDivider = false,
    });

    ScopedFont font{ 12, FW_NORMAL };
    const ScopedGdiObject selectedFont(dc, font.handle);
    DrawVisibilityCell(dc, layout, theme, row.visible);

    if (row.hasChildren) {
        DrawExpander(dc, layout.expanderIcon, theme, row.expanded);
    }

    const COLORREF entityIcon = GdiDrawing::ToColorRef(row.prefabRoot ? theme.accent : theme.textSecondary);
    HeroIconPainter::Draw(
        dc,
        layout.entityIcon,
        row.hasCamera ? HeroIconKind::Camera : HeroIconKind::Cube,
        row.visible ? entityIcon : GdiDrawing::ToColorRef(theme.textDisabled),
        2);
    if (sceneContext.IsHierarchyRenaming(row.entity)) {
        DrawRenameField(dc, layout.label, theme, sceneContext.HierarchyRenameBuffer(), sceneContext.IsHierarchyRenameSelectingAll());
    }
}

} // namespace kb::editor

#endif
