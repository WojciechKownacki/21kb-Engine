#include "rendering/ProjectFilesBottomBarRenderer.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserState.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/ProjectFilesPanelDrawing.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

using Draw = ProjectFilesPanelDrawing;

[[nodiscard]] const char* SortLabel(EditorAssetSortMode mode) noexcept {
    switch (mode) {
    case EditorAssetSortMode::Type:
        return "Sort: Type";
    case EditorAssetSortMode::Path:
        return "Sort: Path";
    case EditorAssetSortMode::Name:
    default:
        return "Sort: Name";
    }
}

void DrawSortMenuItem(HDC dc, RECT rect, const EditorTheme& theme, const char* label, bool selected) {
    GdiDrawing::DrawSharpFrame(dc, rect, selected ? Draw::Blend(Draw::Color(theme.panel), RGB(96, 108, 126), 34) : Draw::Color(theme.panel), Draw::Color(theme.borderPanel));
    Draw::DrawLabel(dc, Draw::Inset(rect, 8, 0), label, selected ? Draw::Color(theme.textPrimary) : Draw::Color(theme.textSecondary));
}

} // namespace

void ProjectFilesBottomBarRenderer::Paint(HDC dc, const EditorAssetBrowserLayoutRects& layout, const EditorTheme& theme, const EditorAssetBrowserState& state) {
    GdiDrawing::DrawSharpFrame(dc, layout.bottomBar, Draw::Blend(Draw::Color(theme.strip), RGB(0, 0, 0), 10), Draw::Color(theme.borderPanel));
    Draw::DrawTextButton(dc, layout.sortButton, theme, SortLabel(state.SortMode()), state.IsSortMenuOpen());
    Draw::DrawIconButton(dc, layout.listButton, theme, HeroIconKind::EllipsisHorizontal, state.ViewMode() == EditorAssetViewMode::List);
    Draw::DrawIconButton(dc, layout.tileButton, theme, HeroIconKind::Cube, state.ViewMode() == EditorAssetViewMode::Tiles);
    Draw::DrawTextButton(dc, layout.recursiveButton, theme, state.Recursive() ? "Recursive" : "Current", state.Recursive());

    RECT sliderLine = layout.sliderTrack;
    GdiDrawing::FillRectColor(dc, sliderLine, Draw::Blend(Draw::Color(theme.borderPanel), Draw::Color(theme.textSecondary), 18));
    const float t = std::clamp((state.ThumbnailScale() - 0.65F) / 1.10F, 0.0F, 1.0F);
    const int thumbCenter = layout.sliderTrack.left + static_cast<int>(static_cast<float>(layout.sliderTrack.right - layout.sliderTrack.left) * t);
    RECT thumb{ thumbCenter - 5, layout.bottomBar.top + 6, thumbCenter + 5, layout.bottomBar.bottom - 6 };
    GdiDrawing::DrawSharpFrame(dc, thumb, Draw::Blend(Draw::Color(theme.accent), Draw::Color(theme.textPrimary), 12), Draw::Color(theme.accent));

    RECT small{ layout.sliderTrack.left - 22, layout.bottomBar.top, layout.sliderTrack.left - 6, layout.bottomBar.bottom };
    RECT large{ layout.sliderTrack.right + 6, layout.bottomBar.top, layout.sliderTrack.right + 24, layout.bottomBar.bottom };
    Draw::DrawCenteredLabel(dc, small, "A", Draw::Color(theme.textDisabled));
    Draw::DrawCenteredLabel(dc, large, "A", Draw::Color(theme.textPrimary));

    if (state.IsSortMenuOpen()) {
        DrawSortMenuItem(dc, layout.sortNameItem, theme, "Name", state.SortMode() == EditorAssetSortMode::Name);
        DrawSortMenuItem(dc, layout.sortTypeItem, theme, "Type", state.SortMode() == EditorAssetSortMode::Type);
        DrawSortMenuItem(dc, layout.sortPathItem, theme, "Path", state.SortMode() == EditorAssetSortMode::Path);
    }
}

} // namespace kb::editor

#endif
