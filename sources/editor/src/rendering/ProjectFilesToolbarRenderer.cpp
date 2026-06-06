#include "rendering/ProjectFilesToolbarRenderer.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserGeometry.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/ProjectFilesPanelDrawing.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {
namespace {

using Draw = ProjectFilesPanelDrawing;

void DrawContentBreadcrumbSegment(HDC dc, RECT rect, const EditorTheme& theme, std::string_view label, bool root, bool last) {
    const COLORREF fill = last ? Draw::Blend(Draw::Color(theme.panel), Draw::Color(theme.strip), 18) : Draw::Blend(Draw::Color(theme.panel), Draw::Color(theme.strip), 10);
    const COLORREF border = last ? Draw::Blend(Draw::Color(theme.borderPanel), Draw::Color(theme.textSecondary), 20) : Draw::Color(theme.borderPanel);
    GdiDrawing::DrawSharpFrame(dc, rect, fill, border);
    if (root) {
        RECT icon{ rect.left + 9, rect.top + 4, rect.left + 25, rect.bottom - 4 };
        HeroIconPainter::Draw(dc, icon, HeroIconKind::Folder, Draw::FolderColor(false), 1);
        RECT text{ icon.right + 6, rect.top, rect.right - 8, rect.bottom };
        Draw::DrawLabel(dc, text, std::string{ label }.c_str(), Draw::Color(theme.textPrimary));
        return;
    }

    Draw::DrawCenteredLabel(dc, Draw::Inset(rect, 8, 0), std::string{ label }.c_str(), last ? Draw::Color(theme.textPrimary) : Draw::Color(theme.textSecondary));
}

void DrawBreadcrumb(HDC dc, RECT rect, const EditorTheme& theme, const std::filesystem::path& folder) {
    const std::vector<std::string> segments = EditorAssetBrowserGeometry::BreadcrumbSegments(folder);
    int left = rect.left;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        const std::string& segment = segments[index];
        const std::string label = EditorAssetBrowserGeometry::BreadcrumbDisplayLabel(segment, index);
        const int width = EditorAssetBrowserGeometry::BreadcrumbSegmentWidth(label, index == 0U);
        RECT part{ left, rect.top, static_cast<LONG>(std::min(left + width, static_cast<int>(rect.right))), rect.bottom };
        if (part.right <= part.left) {
            break;
        }
        DrawContentBreadcrumbSegment(dc, part, theme, label, index == 0U, index + 1 == segments.size());
        left += width;
        if (index + 1 < segments.size() && left < rect.right - 12) {
            RECT separator{ left, rect.top, left + 10, rect.bottom };
            Draw::DrawCenteredLabel(dc, separator, "/", Draw::Color(theme.textDisabled));
            left += 10;
        }
    }
}

void DrawSearch(HDC dc, RECT rect, const EditorTheme& theme, const EditorAssetBrowserState& state) {
    const COLORREF border = state.IsSearchFocused() ? Draw::Blend(Draw::Color(theme.textSecondary), Draw::Color(theme.accent), 28) : Draw::Color(theme.borderPanel);
    GdiDrawing::DrawSharpFrame(dc, rect, RGB(18, 20, 24), border);
    RECT accent{ rect.left + 1, rect.top + 5, rect.left + 3, rect.bottom - 5 };
    if (state.IsSearchFocused() || !state.SearchQuery().empty()) {
        GdiDrawing::FillRectColor(dc, accent, Draw::Color(theme.accent));
    }
    RECT icon{ rect.left + 7, rect.top + 5, rect.left + 23, rect.bottom - 5 };
    HeroIconPainter::Draw(dc, icon, HeroIconKind::MagnifyingGlass, Draw::Color(theme.textSecondary), 2);

    RECT text{ rect.left + 30, rect.top, rect.right - 8, rect.bottom };
    const std::string query{ state.SearchQuery() };
    Draw::DrawLabel(dc, text, query.empty() ? "Szukaj zawartosci..." : query.c_str(), query.empty() ? Draw::Color(theme.textDisabled) : Draw::Color(theme.textPrimary));
}

} // namespace

void ProjectFilesToolbarRenderer::Paint(HDC dc, const EditorAssetBrowserLayoutRects& layout, const EditorTheme& theme, const EditorAssetBrowserState& state) {
    GdiDrawing::DrawSharpFrame(dc, layout.toolbar, Draw::Blend(Draw::Color(theme.strip), RGB(0, 0, 0), 10), Draw::Color(theme.borderPanel));
    RECT title{ layout.toolbar.left + 8, layout.toolbar.top, layout.toolbar.left + 126, layout.toolbar.bottom };
    Draw::DrawTextWithFont(dc, title, "Content", Draw::Color(theme.textPrimary), 12, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    Draw::DrawIconButton(dc, layout.newFolderButton, theme, HeroIconKind::Plus, state.TextEditMode() == EditorAssetTextEditMode::NewFolder);
    Draw::DrawTextButton(dc, layout.filtersButton, theme, "Filters", !state.TypeFilter().empty());
    DrawBreadcrumb(dc, layout.path, theme, state.SelectedFolder());
    DrawSearch(dc, layout.search, theme, state);
}

} // namespace kb::editor

#endif
