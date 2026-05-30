#include "rendering/HierarchyPanelToolbarRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/HierarchyPanelStyle.hpp"
#include "rendering/HierarchyToolbarLayout.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <string>

namespace kb::editor {

namespace {

void DrawToolbarButton(HDC dc, const RECT& rect, HeroIconKind icon) {
    const RECT iconRect = HierarchyToolbarLayout::IconRect(rect);
    HeroIconPainter::Draw(dc, iconRect, icon, HierarchyPanelStyle::MutedText(), 2);
}

} // namespace

RECT HierarchyPanelToolbarRenderer::Paint(HDC dc, const RECT& content, const EditorTheme&, const EditorSceneContext& sceneContext) const {
    const HierarchyToolbarLayoutRects layout = HierarchyToolbarLayout::Resolve(content);
    GdiDrawing::FillRectColor(dc, layout.header, HierarchyPanelStyle::HeaderBackground());

    GdiDrawing::FillRectColor(dc, layout.bottomLine, HierarchyPanelStyle::HeaderBorder());

    ScopedFont font{ 12, FW_NORMAL };
    const ScopedGdiObject selectedFont(dc, font.handle);

    DrawToolbarButton(dc, layout.addButton, HeroIconKind::Plus);

    GdiDrawing::DrawSharpFrame(
        dc,
        layout.searchBox,
        HierarchyPanelStyle::SearchBackground(),
        sceneContext.IsHierarchySearchFocused() ? HierarchyPanelStyle::SearchFocusedBorder() : HierarchyPanelStyle::SearchBorder());
    const std::string_view query = sceneContext.HierarchySearchQuery();
    const std::string searchText = query.empty() ? std::string{ "Search" } : std::string{ query };
    GdiDrawing::DrawTextBlock(
        dc,
        layout.searchText,
        searchText.c_str(),
        query.empty() ? HierarchyPanelStyle::SearchPlaceholderText() : HierarchyPanelStyle::RowText());
    HeroIconPainter::Draw(dc, layout.searchIcon, HeroIconKind::MagnifyingGlass, HierarchyPanelStyle::MutedText(), 2);

    DrawToolbarButton(dc, layout.optionsButton, HeroIconKind::EllipsisHorizontal);

    return layout.listContent;
}

} // namespace kb::editor

#endif
