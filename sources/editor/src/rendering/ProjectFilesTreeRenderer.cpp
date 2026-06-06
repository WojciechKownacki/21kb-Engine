#include "rendering/ProjectFilesTreeRenderer.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserState.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/ProjectFilesPanelDrawing.hpp"

namespace kb::editor {

void ProjectFilesTreeRenderer::Paint(
    HDC dc,
    const EditorAssetBrowserLayoutRects& layout,
    const EditorTheme& theme,
    const EditorAssetBrowserState& state,
    const std::vector<EditorAssetFolderRow>& folders) {
    using Draw = ProjectFilesPanelDrawing;

    GdiDrawing::DrawSharpFrame(dc, layout.tree, Draw::Blend(Draw::Color(theme.panel), RGB(0, 0, 0), 5), Draw::Color(theme.borderPanel));
    RECT header{ layout.tree.left + 1, layout.tree.top + 1, layout.tree.right - 1, layout.tree.top + 30 };
    GdiDrawing::FillRectColor(dc, header, Draw::Blend(Draw::Color(theme.strip), RGB(0, 0, 0), 8));
    Draw::DrawTextWithFont(dc, RECT{ header.left + 10, header.top, header.right - 10, header.bottom }, "Folders", Draw::Color(theme.textSecondary), 12, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    Draw::DrawHairline(dc, RECT{ layout.tree.left + 1, header.bottom, layout.tree.right - 1, header.bottom + 1 }, Draw::Color(theme.borderPanel));
    for (std::size_t index = 0; index < folders.size(); ++index) {
        RECT row = EditorAssetBrowserLayout::FolderRowRect(layout, static_cast<int>(index));
        if (row.top >= layout.tree.bottom - 4) {
            break;
        }

        if (folders[index].selected) {
            GdiDrawing::FillRectColor(dc, row, Draw::Blend(Draw::Color(theme.panel), RGB(94, 103, 118), state.IsSelectionFocused() ? 36 : 22));
        }

        const int indent = folders[index].depth * 14;
        RECT disclosure{ row.left + 3 + indent, row.top + 4, row.left + 16 + indent, row.bottom - 4 };
        if (folders[index].hasChildren) {
            Draw::DrawDisclosureTriangle(dc, disclosure, folders[index].selected ? Draw::Color(theme.textPrimary) : Draw::Color(theme.textDisabled), folders[index].expanded);
        }

        RECT icon{ disclosure.right + 3, row.top + 4, disclosure.right + 20, row.bottom - 4 };
        HeroIconPainter::Draw(dc, icon, HeroIconKind::Folder, folders[index].selected ? Draw::FolderColor(true) : Draw::FolderColor(false), 1);
        RECT text{ icon.right + 6, row.top, row.right, row.bottom };
        const bool renamingThisFolder = state.TextEditMode() == EditorAssetTextEditMode::RenameFolder
            && Draw::SameVirtualPath(folders[index].virtualPath, state.TextEditTargetFolder());
        if (renamingThisFolder) {
            Draw::DrawEditField(dc, text, theme, state.TextEditValue());
        } else {
            Draw::DrawLabel(dc, text, folders[index].name.c_str(), folders[index].selected ? Draw::Color(theme.textPrimary) : Draw::Color(theme.textSecondary));
        }
    }
}

} // namespace kb::editor

#endif
