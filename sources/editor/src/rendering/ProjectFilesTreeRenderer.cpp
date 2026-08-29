#include "rendering/ProjectFilesTreeRenderer.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserState.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/ProjectFilesPanelDrawing.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

void DrawScrollbar(HDC dc, const EditorAssetBrowserLayoutRects& layout, const EditorTheme& theme, const EditorAssetBrowserState& state, int contentHeight) {
    const RECT viewport = EditorAssetBrowserLayout::TreeViewportRect(layout);
    const int viewportHeight = static_cast<int>(viewport.bottom - viewport.top);
    if (contentHeight <= viewportHeight) {
        return;
    }
    const RECT track = EditorAssetBrowserLayout::TreeScrollbarTrackRect(layout);
    const RECT thumb = EditorAssetBrowserLayout::ScrollbarThumbRect(track, viewportHeight, contentHeight, state.TreeScrollOffset());
    using Draw = ProjectFilesPanelDrawing;
    GdiDrawing::DrawSharpFrame(dc, track, Draw::Color(theme.chrome), Draw::Color(theme.borderChrome));
    const COLORREF thumbColor = Draw::Color(state.IsTreeScrollbarDragging() ? theme.accent : theme.borderPanel);
    const COLORREF thumbBorder = Draw::Color(state.IsTreeScrollbarDragging() ? theme.textSecondary : theme.borderPanel);
    GdiDrawing::DrawSharpFrame(dc, thumb, thumbColor, thumbBorder);
}

} // namespace

void ProjectFilesTreeRenderer::Paint(
    HDC dc,
    const EditorAssetBrowserLayoutRects& layout,
    const EditorTheme& theme,
    const EditorAssetBrowserState& state,
    const std::vector<EditorAssetFolderRow>& folders) {
    using Draw = ProjectFilesPanelDrawing;

    GdiDrawing::DrawSharpFrame(dc, layout.tree, Draw::Blend(Draw::Color(theme.panel), RGB(0, 0, 0), 5), Draw::Color(theme.borderPanel));
    RECT header{ layout.tree.left + 1, layout.tree.top + 1, layout.tree.right - 1, layout.toolbar.bottom - 1 };
    GdiDrawing::FillRectColor(dc, header, Draw::Blend(Draw::Color(theme.strip), RGB(0, 0, 0), 8));
    Draw::DrawTextWithFont(dc, RECT{ header.left + 10, header.top, header.right - 10, header.bottom }, "Folders", Draw::Color(theme.textSecondary), 12, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    Draw::DrawHairline(dc, RECT{ layout.tree.left + 1, header.bottom, layout.tree.right - 1, header.bottom + 1 }, Draw::Color(theme.borderPanel));
    const RECT viewport = EditorAssetBrowserLayout::TreeViewportRect(layout);
    const int contentHeight = static_cast<int>(folders.size()) * EditorAssetBrowserLayout::RowHeight;
    const int maxOffset = std::max(0, contentHeight - static_cast<int>(viewport.bottom - viewport.top));
    const int scroll = std::clamp(state.TreeScrollOffset(), 0, maxOffset);
    const int firstRow = std::max(0, scroll / EditorAssetBrowserLayout::RowHeight);
    const int yOffset = scroll % EditorAssetBrowserLayout::RowHeight;
    SaveDC(dc);
    IntersectClipRect(dc, viewport.left, viewport.top, viewport.right, viewport.bottom);
    for (std::size_t index = static_cast<std::size_t>(firstRow); index < folders.size(); ++index) {
        RECT row = EditorAssetBrowserLayout::FolderRowRect(layout, static_cast<int>(index));
        OffsetRect(&row, 0, -scroll);
        if (row.top >= layout.tree.bottom - 1) {
            break;
        }
        if (row.bottom <= viewport.top - yOffset - EditorAssetBrowserLayout::RowHeight) {
            continue;
        }

        if (folders[index].selected) {
            GdiDrawing::FillRectColor(dc, row, Draw::Blend(Draw::Color(theme.panel), Draw::Color(theme.accent), state.IsSelectionFocused() ? 18 : 10));
            if (state.IsSelectionFocused()) {
                GdiDrawing::FillRectColor(dc, RECT{ row.left, row.top, row.left + 3, row.bottom }, Draw::Color(theme.accent));
            }
        }

        const int indent = folders[index].depth * 14;
        RECT disclosure{ row.left + 3 + indent, row.top + 4, row.left + 16 + indent, row.bottom - 4 };
        if (folders[index].hasChildren) {
            Draw::DrawDisclosureTriangle(dc, disclosure, folders[index].selected ? Draw::Color(theme.textPrimary) : Draw::Color(theme.textDisabled), folders[index].expanded);
        }

        RECT icon{ disclosure.right + 3, row.top + 4, disclosure.right + 20, row.bottom - 4 };
        Draw::DrawIconWithShadow(dc, icon, HeroIconKind::Folder, folders[index].selected ? Draw::FolderColor(true) : Draw::FolderColor(false), 1);
        RECT text{ icon.right + 6, row.top, row.right, row.bottom };
        const bool renamingThisFolder = state.TextEditMode() == EditorAssetTextEditMode::RenameFolder
            && Draw::SameVirtualPath(folders[index].virtualPath, state.TextEditTargetFolder());
        if (renamingThisFolder) {
            Draw::DrawEditField(dc, text, theme, state.TextEditValue());
        } else {
            Draw::DrawLabel(dc, text, folders[index].name.c_str(), folders[index].selected ? Draw::Color(theme.textPrimary) : Draw::Color(theme.textSecondary));
        }
    }
    RestoreDC(dc, -1);
    DrawScrollbar(dc, layout, theme, state, contentHeight);
}

} // namespace kb::editor

#endif
