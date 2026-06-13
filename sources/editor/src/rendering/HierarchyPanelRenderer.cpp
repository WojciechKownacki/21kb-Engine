#include "rendering/HierarchyPanelRenderer.hpp"

#if defined(_WIN32)
#include "rendering/EditorSurfacePainter.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HierarchyPanelStyle.hpp"
#include "rendering/HierarchyPanelToolbarRenderer.hpp"
#include "rendering/HierarchyRowRenderer.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "scene/EditorHierarchyMetrics.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

constexpr int kHierarchyScrollbarWidth = 12;
constexpr int kHierarchyScrollbarInset = 3;
constexpr int kHierarchyScrollbarMinThumb = 24;
constexpr int kContextMenuWidth = 128;
constexpr int kLightingSubmenuWidth = 190;
constexpr int kLightingSubmenuGap = 4;
constexpr int kContextMenuRowHeight = 24;
constexpr int kContextMenuPadding = 4;
constexpr int kContextMenuItemCount = 1;
constexpr int kLightingSubmenuRows = 4;

[[nodiscard]] int RectHeight(const RECT& rect) noexcept {
    return std::max(0L, rect.bottom - rect.top);
}

[[nodiscard]] RECT ScrollbarTrack(const RECT& listContent) noexcept {
    return RECT{
        .left = listContent.right - kHierarchyScrollbarWidth,
        .top = listContent.top + kHierarchyScrollbarInset,
        .right = listContent.right - kHierarchyScrollbarInset,
        .bottom = listContent.bottom - kHierarchyScrollbarInset,
    };
}

[[nodiscard]] RECT ScrollbarThumb(const RECT& track, int viewportHeight, int contentHeight, int offset) noexcept {
    const int trackHeight = RectHeight(track);
    if (trackHeight <= 0 || contentHeight <= viewportHeight) {
        return {};
    }

    const int thumbHeight = std::clamp((trackHeight * viewportHeight) / std::max(1, contentHeight), kHierarchyScrollbarMinThumb, trackHeight);
    const int maxOffset = std::max(1, contentHeight - viewportHeight);
    const int travel = std::max(0, trackHeight - thumbHeight);
    const int thumbTop = track.top + (travel * std::clamp(offset, 0, maxOffset)) / maxOffset;
    return RECT{ track.left + 2, thumbTop, track.right - 2, thumbTop + thumbHeight };
}

void DrawScrollbar(HDC dc, const RECT& listContent, const EditorSceneContext& sceneContext, int contentHeight) {
    const int viewportHeight = RectHeight(listContent);
    if (contentHeight <= viewportHeight) {
        return;
    }

    const RECT track = ScrollbarTrack(listContent);
    const RECT thumb = ScrollbarThumb(track, viewportHeight, contentHeight, sceneContext.HierarchyScrollOffset());
    GdiDrawing::DrawSharpFrame(dc, track, RGB(22, 24, 27), RGB(38, 42, 47));
    const COLORREF thumbColor = sceneContext.IsHierarchyScrollbarDragging() ? RGB(104, 116, 130) : RGB(76, 86, 98);
    const COLORREF thumbBorder = sceneContext.IsHierarchyScrollbarDragging() ? RGB(128, 142, 158) : RGB(94, 105, 118);
    GdiDrawing::DrawSharpFrame(dc, thumb, thumbColor, thumbBorder);
}

[[nodiscard]] RECT ContextMenuRect(const RECT& content, const EditorSceneContext& sceneContext) noexcept {
    const int width = kContextMenuWidth;
    const int height = kContextMenuPadding * 2 + kContextMenuItemCount * kContextMenuRowHeight;
    int x = std::clamp(sceneContext.HierarchyContextMenuX(), static_cast<int>(content.left), std::max(static_cast<int>(content.left), static_cast<int>(content.right) - width));
    const int y = std::clamp(sceneContext.HierarchyContextMenuY(), static_cast<int>(content.top), std::max(static_cast<int>(content.top), static_cast<int>(content.bottom) - height));
    return RECT{ x, y, x + width, y + height };
}

[[nodiscard]] RECT ContextMenuItemRect(const RECT& menu, int index) noexcept {
    const int top = menu.top + kContextMenuPadding + index * kContextMenuRowHeight;
    return RECT{ menu.left + kContextMenuPadding, top, menu.right - kContextMenuPadding, top + kContextMenuRowHeight };
}

[[nodiscard]] const char* ContextMenuLabel(EditorHierarchyContextCommand command) noexcept {
    switch (command) {
    case EditorHierarchyContextCommand::AddDirectionalLight:
        return "Directional Light";
    case EditorHierarchyContextCommand::AddPointLight:
        return "Point Light";
    case EditorHierarchyContextCommand::AddSpotLight:
        return "Spot Light";
    case EditorHierarchyContextCommand::AddLighting:
        return "Add";
    case EditorHierarchyContextCommand::None:
    default:
        return "";
    }
}

[[nodiscard]] EditorHierarchyContextCommand ContextMenuCommandAt(int index) noexcept {
    switch (index) {
    case 0:
        return EditorHierarchyContextCommand::AddLighting;
    default:
        return EditorHierarchyContextCommand::None;
    }
}

[[nodiscard]] bool ShowsLightingSubmenu(EditorHierarchyContextCommand command) noexcept {
    return command == EditorHierarchyContextCommand::AddLighting
        || command == EditorHierarchyContextCommand::AddDirectionalLight
        || command == EditorHierarchyContextCommand::AddPointLight
        || command == EditorHierarchyContextCommand::AddSpotLight;
}

[[nodiscard]] RECT LightingSubmenuRect(const RECT& content, const RECT& menu) noexcept {
    const RECT addRow = ContextMenuItemRect(menu, 0);
    RECT submenu{
        addRow.right + kLightingSubmenuGap,
        addRow.top,
        addRow.right + kLightingSubmenuGap + kLightingSubmenuWidth,
        addRow.top + kContextMenuPadding * 2 + kLightingSubmenuRows * kContextMenuRowHeight,
    };
    if (submenu.right > content.right) {
        const int width = submenu.right - submenu.left;
        const int height = submenu.bottom - submenu.top;
        const int left = std::clamp(static_cast<int>(menu.left), static_cast<int>(content.left), std::max(static_cast<int>(content.left), static_cast<int>(content.right) - width));
        int top = menu.bottom + kLightingSubmenuGap;
        if (top + height > content.bottom) {
            top = menu.top - height - kLightingSubmenuGap;
        }
        top = std::clamp(top, static_cast<int>(content.top), std::max(static_cast<int>(content.top), static_cast<int>(content.bottom) - height));
        submenu = RECT{ left, top, left + width, top + height };
    } else if (submenu.bottom > content.bottom) {
        OffsetRect(&submenu, 0, content.bottom - submenu.bottom);
    }
    return submenu;
}

void DrawContextMenu(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    (void)theme;
    if (!sceneContext.IsHierarchyContextMenuOpen()) {
        return;
    }

    const RECT menu = ContextMenuRect(content, sceneContext);
    RECT shadow = menu;
    OffsetRect(&shadow, 3, 3);
    GdiDrawing::FillRectAlpha(dc, shadow, RGB(0, 0, 0), 90);
    GdiDrawing::DrawSharpFrame(dc, menu, RGB(27, 29, 34), RGB(52, 58, 66));

    for (int index = 0; index < kContextMenuItemCount; ++index) {
        const EditorHierarchyContextCommand command = ContextMenuCommandAt(index);
        const RECT row = ContextMenuItemRect(menu, index);
        const bool hovered = sceneContext.HierarchyContextMenuHoveredCommand() == command
            || (command == EditorHierarchyContextCommand::AddLighting && ShowsLightingSubmenu(sceneContext.HierarchyContextMenuHoveredCommand()));
        if (hovered) {
            GdiDrawing::FillRectAlpha(dc, row, RGB(70, 122, 166), 38);
        }
        RECT label{ row.left + 9, row.top, row.right - 8, row.bottom };
        ScopedFont font{ 12, FW_NORMAL };
        const ScopedGdiObject selectedFont(dc, font.handle);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, hovered ? RGB(232, 236, 240) : RGB(168, 176, 186));
        DrawTextA(dc, ContextMenuLabel(command), -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        RECT arrow{ row.right - 20, row.top, row.right - 6, row.bottom };
        DrawTextA(dc, ">", -1, &arrow, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    if (!ShowsLightingSubmenu(sceneContext.HierarchyContextMenuHoveredCommand())) {
        return;
    }

    const RECT submenu = LightingSubmenuRect(content, menu);
    RECT submenuShadow = submenu;
    OffsetRect(&submenuShadow, 3, 3);
    GdiDrawing::FillRectAlpha(dc, submenuShadow, RGB(0, 0, 0), 90);
    GdiDrawing::DrawSharpFrame(dc, submenu, RGB(27, 29, 34), RGB(52, 58, 66));

    constexpr EditorHierarchyContextCommand rows[] = {
        EditorHierarchyContextCommand::AddLighting,
        EditorHierarchyContextCommand::AddDirectionalLight,
        EditorHierarchyContextCommand::AddPointLight,
        EditorHierarchyContextCommand::AddSpotLight,
    };
    constexpr const char* labels[] = {
        "Lighting",
        "Directional Light",
        "Point Light",
        "Spot Light",
    };
    for (int index = 0; index < kLightingSubmenuRows; ++index) {
        const RECT row = ContextMenuItemRect(submenu, index);
        const bool hovered = sceneContext.HierarchyContextMenuHoveredCommand() == rows[index]
            && rows[index] != EditorHierarchyContextCommand::AddLighting;
        if (hovered) {
            GdiDrawing::FillRectAlpha(dc, row, RGB(70, 122, 166), 38);
        }
        RECT label{ row.left + 9, row.top, row.right - 8, row.bottom };
        ScopedFont font{ 12, index == 0 ? FW_SEMIBOLD : FW_NORMAL };
        const ScopedGdiObject selectedFont(dc, font.handle);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, hovered ? RGB(232, 236, 240) : RGB(168, 176, 186));
        DrawTextA(dc, labels[index], -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
}

} // namespace

void HierarchyPanelRenderer::Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext) const {
    const std::vector<EditorHierarchyRow>& rows = sceneContext.HierarchyRows();

    GdiDrawing::FillRectColor(dc, content, HierarchyPanelStyle::PanelBackground());
    const RECT listContent = HierarchyPanelToolbarRenderer{}.Paint(dc, content, theme, sceneContext);
    const int contentHeight = static_cast<int>(rows.size()) * kHierarchyRowHeight;
    const int viewportHeight = RectHeight(listContent);
    const int maxOffset = std::max(0, contentHeight - viewportHeight);
    const int scroll = std::clamp(sceneContext.HierarchyScrollOffset(), 0, maxOffset);
    const bool hasScrollbar = contentHeight > viewportHeight;
    const int rowsRight = hasScrollbar ? listContent.right - kHierarchyScrollbarWidth : listContent.right;

    if (viewportHeight > 0 && !rows.empty()) {
        const std::size_t firstRow = static_cast<std::size_t>(scroll / kHierarchyRowHeight);
        const int firstRowOffset = scroll % kHierarchyRowHeight;
        const std::size_t visibleRows = static_cast<std::size_t>((viewportHeight + firstRowOffset + kHierarchyRowHeight - 1) / kHierarchyRowHeight);
        const std::size_t lastRow = std::min(rows.size(), firstRow + visibleRows);
        int y = listContent.top - firstRowOffset;
        for (std::size_t rowIndex = firstRow; rowIndex < lastRow; ++rowIndex) {
            const EditorHierarchyRow& row = rows[rowIndex];
            RECT rowRect{ listContent.left, y, rowsRight, y + kHierarchyRowHeight };
            HierarchyRowRenderer{}.Paint(dc, rowRect, theme, row, sceneContext);
            y += kHierarchyRowHeight;
        }
    }
    DrawScrollbar(dc, listContent, sceneContext, contentHeight);
}

void HierarchyPanelRenderer::PaintContextMenu(HDC dc, const RECT& bounds, const EditorTheme& theme, const EditorSceneContext& sceneContext) const {
    DrawContextMenu(dc, bounds, theme, sceneContext);
}

} // namespace kb::editor

#endif
