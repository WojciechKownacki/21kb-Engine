#include "rendering/HierarchyPanelRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/HierarchyPanelStyle.hpp"
#include "rendering/HierarchyPanelToolbarRenderer.hpp"
#include "rendering/HierarchyRowRenderer.hpp"
#include "scene/EditorHierarchyMetrics.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

constexpr int kHierarchyScrollbarWidth = 12;
constexpr int kHierarchyScrollbarInset = 3;
constexpr int kHierarchyScrollbarMinThumb = 24;

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

} // namespace kb::editor

#endif
