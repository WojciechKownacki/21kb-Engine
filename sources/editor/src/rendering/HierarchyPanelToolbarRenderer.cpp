#include "rendering/HierarchyPanelToolbarRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/HierarchyToolbarLayout.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <string>

namespace kb::editor {

namespace {

void DrawToolbarButton(HDC dc, const RECT& rect, const EditorTheme& theme, HeroIconKind icon) {
    const RECT iconRect = HierarchyToolbarLayout::IconRect(rect);
    HeroIconPainter::Draw(dc, iconRect, icon, GdiDrawing::ToColorRef(theme.textSecondary), 2);
}

} // namespace

RECT HierarchyPanelToolbarRenderer::Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext) const {
    const HierarchyToolbarLayoutRects layout = HierarchyToolbarLayout::Resolve(content);
    GdiDrawing::FillRectColor(dc, layout.header, GdiDrawing::ToColorRef(theme.strip));

    GdiDrawing::FillRectColor(dc, layout.bottomLine, GdiDrawing::ToColorRef(theme.borderChrome));

    ScopedFont font{ 12, FW_NORMAL };
    const ScopedGdiObject selectedFont(dc, font.handle);

    DrawToolbarButton(dc, layout.addButton, theme, HeroIconKind::Plus);

    GdiDrawing::DrawSharpFrame(
        dc,
        layout.searchBox,
        GdiDrawing::ToColorRef(theme.chrome),
        GdiDrawing::ToColorRef(sceneContext.IsHierarchySearchFocused() ? theme.accent : theme.borderPanel));
    const std::string_view query = sceneContext.HierarchySearchQuery();
    const std::string searchText = query.empty() ? std::string{ "Search" } : std::string{ query };
    GdiDrawing::DrawTextBlock(
        dc,
        layout.searchText,
        searchText.c_str(),
        GdiDrawing::ToColorRef(query.empty() ? theme.textDisabled : theme.textPrimary));
    HeroIconPainter::Draw(dc, layout.searchIcon, HeroIconKind::MagnifyingGlass, GdiDrawing::ToColorRef(theme.textSecondary), 2);

    DrawToolbarButton(dc, layout.optionsButton, theme, HeroIconKind::EllipsisHorizontal);

    return layout.listContent;
}

} // namespace kb::editor

#endif
