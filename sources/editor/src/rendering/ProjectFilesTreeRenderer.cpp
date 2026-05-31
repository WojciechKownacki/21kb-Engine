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

    GdiDrawing::DrawSharpFrame(dc, layout.tree, Draw::Color(theme.panel), Draw::Color(theme.borderPanel));
    for (std::size_t index = 0; index < folders.size(); ++index) {
        RECT row = EditorAssetBrowserLayout::FolderRowRect(layout, static_cast<int>(index));
        if (row.top >= layout.tree.bottom - 4) {
            break;
        }

        if (folders[index].selected) {
            GdiDrawing::FillRectAlpha(dc, row, Draw::Color(theme.accent), 64);
        }

        const int indent = folders[index].depth * 14;
        RECT disclosure{ row.left + indent, row.top + 4, row.left + indent + 13, row.bottom - 4 };
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
