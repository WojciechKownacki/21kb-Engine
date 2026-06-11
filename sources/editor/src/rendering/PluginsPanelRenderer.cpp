#include "rendering/PluginsPanelRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "scene/EditorPluginCatalog.hpp"

#include <algorithm>
#include <string>

namespace kb::editor {
namespace {

constexpr int kHeaderHeight = 42;
constexpr int kColumnHeaderHeight = 28;
constexpr int kRowHeight = 34;
constexpr int kPadding = 14;
constexpr int kScrollbarWidth = 12;
constexpr int kScrollbarInset = 3;
constexpr int kScrollbarMinThumb = 24;
constexpr int kPluginColumnLeft = 30;
constexpr int kPluginColumnRight = 230;
constexpr int kStatusColumnRight = 330;
constexpr int kCategoryColumnRight = 430;

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] int RectHeight(const RECT& rect) noexcept {
    return std::max(0L, rect.bottom - rect.top);
}

[[nodiscard]] RECT HeaderRect(const RECT& content) noexcept {
    return RECT{ content.left, content.top, content.right, std::min(content.bottom, content.top + kHeaderHeight) };
}

[[nodiscard]] RECT ColumnHeaderRect(const RECT& content) noexcept {
    return RECT{ content.left, content.top + kHeaderHeight, content.right, std::min(content.bottom, content.top + kHeaderHeight + kColumnHeaderHeight) };
}

[[nodiscard]] RECT ListRect(const RECT& content) noexcept {
    return RECT{ content.left, std::min(content.bottom, content.top + kHeaderHeight + kColumnHeaderHeight), content.right, content.bottom };
}

[[nodiscard]] RECT ListRowsRect(const RECT& content) noexcept {
    RECT list = ListRect(content);
    list.left += kPadding;
    list.right -= kPadding + kScrollbarWidth;
    return list;
}

[[nodiscard]] RECT ScrollbarTrackRect(const RECT& content) noexcept {
    const RECT list = ListRect(content);
    return RECT{
        .left = list.right - kPadding - kScrollbarWidth + 2,
        .top = list.top + kScrollbarInset,
        .right = list.right - kPadding - 2,
        .bottom = list.bottom - kScrollbarInset,
    };
}

[[nodiscard]] std::int64_t ContentHeight() noexcept {
    return static_cast<std::int64_t>(EditorPluginCatalog::Count()) * kRowHeight;
}

[[nodiscard]] RECT ScrollbarThumbRect(const RECT& content, const EditorSceneContext& sceneContext) noexcept {
    const RECT rows = ListRowsRect(content);
    const RECT track = ScrollbarTrackRect(content);
    const int viewportHeight = RectHeight(rows);
    const std::int64_t contentHeight = ContentHeight();
    const int trackHeight = RectHeight(track);
    if (trackHeight <= 0 || contentHeight <= viewportHeight) {
        return {};
    }

    const int thumbHeight = std::clamp(static_cast<int>((static_cast<std::int64_t>(trackHeight) * viewportHeight) / std::max(std::int64_t{ 1 }, contentHeight)), kScrollbarMinThumb, trackHeight);
    const std::int64_t maxOffset = std::max(std::int64_t{ 1 }, contentHeight - viewportHeight);
    const int travel = std::max(0, trackHeight - thumbHeight);
    const int thumbTop = track.top + static_cast<int>((static_cast<std::int64_t>(travel) * std::clamp(sceneContext.Plugins().ScrollOffset(), std::int64_t{ 0 }, maxOffset)) / maxOffset);
    return RECT{ track.left + 2, thumbTop, track.right - 2, thumbTop + thumbHeight };
}

void DrawText(HDC dc, RECT rect, const char* text, COLORREF color, int pointSize = 12, int weight = FW_NORMAL, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    ScopedFont font{ pointSize, weight };
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, static_cast<int>(flags | DT_NOPREFIX));
}

void DrawCheckbox(HDC dc, RECT rect, bool enabled) {
    const COLORREF fill = enabled ? RGB(46, 95, 138) : RGB(34, 37, 42);
    const COLORREF border = enabled ? RGB(79, 129, 184) : RGB(58, 61, 66);
    GdiDrawing::DrawSharpFrame(dc, rect, fill, border);
    if (enabled) {
        DrawText(dc, rect, "x", RGB(232, 236, 240), 12, FW_BOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void DrawHeader(HDC dc, const RECT& content) {
    const RECT header = HeaderRect(content);
    GdiDrawing::FillRectColor(dc, header, RGB(32, 35, 39));
    GdiDrawing::FillRectColor(dc, RECT{ header.left, header.bottom - 1, header.right, header.bottom }, RGB(13, 14, 16));
    DrawText(dc, RECT{ header.left + kPadding, header.top, header.right - kPadding, header.bottom }, "Plugins", RGB(226, 230, 235), 14, FW_SEMIBOLD);
}

void DrawColumnHeader(HDC dc, const RECT& content) {
    const RECT header = ColumnHeaderRect(content);
    GdiDrawing::FillRectColor(dc, header, RGB(24, 27, 31));
    DrawText(dc, RECT{ header.left + kPadding + kPluginColumnLeft, header.top, header.left + kPluginColumnRight, header.bottom }, "PLUGIN", RGB(150, 158, 168), 11, FW_SEMIBOLD);
    DrawText(dc, RECT{ header.left + kPluginColumnRight, header.top, header.left + kStatusColumnRight, header.bottom }, "STATUS", RGB(150, 158, 168), 11, FW_SEMIBOLD);
    DrawText(dc, RECT{ header.left + kStatusColumnRight, header.top, header.left + kCategoryColumnRight, header.bottom }, "CATEGORY", RGB(150, 158, 168), 11, FW_SEMIBOLD);
    DrawText(dc, RECT{ header.left + kCategoryColumnRight, header.top, header.right - kPadding, header.bottom }, "BINARY", RGB(150, 158, 168), 11, FW_SEMIBOLD);
    GdiDrawing::FillRectColor(dc, RECT{ header.left, header.bottom - 1, header.right, header.bottom }, RGB(13, 14, 16));
}

void DrawRow(HDC dc, const RECT& row, std::size_t index, const EditorSceneContext& sceneContext) {
    const EditorPluginDescriptor* plugin = EditorPluginCatalog::At(index);
    if (plugin == nullptr) {
        return;
    }

    const bool hovered = sceneContext.Plugins().HoveredPluginIndex() == index;
    const bool enabled = sceneContext.IsProjectPluginEnabled(plugin->id);
    GdiDrawing::FillRectColor(dc, row, hovered ? RGB(35, 43, 52) : ((index & 1U) != 0U ? RGB(28, 31, 35) : RGB(26, 28, 31)));
    if (enabled) {
        GdiDrawing::FillRectColor(dc, RECT{ row.left, row.top, row.left + 2, row.bottom }, RGB(79, 129, 184));
    }

    const RECT checkbox{ row.left + 8, row.top + 8, row.left + 24, row.top + 24 };
    DrawCheckbox(dc, checkbox, enabled);
    DrawText(dc, RECT{ row.left + 34, row.top + 1, row.left + kPluginColumnRight - 10, row.top + 18 }, plugin->displayName.data(), RGB(222, 228, 234), 12, FW_SEMIBOLD);
    DrawText(dc, RECT{ row.left + 34, row.top + 17, row.left + kPluginColumnRight - 10, row.bottom }, plugin->id.data(), RGB(122, 130, 144), 11);
    DrawText(dc, RECT{ row.left + kPluginColumnRight, row.top, row.left + kStatusColumnRight, row.bottom }, enabled ? "Enabled" : "Disabled", enabled ? RGB(126, 201, 143) : RGB(136, 145, 156), 12);
    DrawText(dc, RECT{ row.left + kStatusColumnRight, row.top, row.left + kCategoryColumnRight, row.bottom }, plugin->category.data(), RGB(196, 205, 214), 12);

    const std::string binary = sceneContext.ProjectPluginBinaryPath(plugin->id).empty()
        ? std::string{ plugin->binaryPath }
        : sceneContext.ProjectPluginBinaryPath(plugin->id);
    DrawText(dc, RECT{ row.left + kCategoryColumnRight, row.top, row.right - 10, row.bottom }, binary.c_str(), RGB(150, 158, 168), 11);
    GdiDrawing::FillRectColor(dc, RECT{ row.left, row.bottom - 1, row.right, row.bottom }, RGB(18, 20, 23));
}

void DrawScrollbar(HDC dc, const RECT& content, const EditorSceneContext& sceneContext) {
    if (PluginsPanelRenderer::MaxScrollOffset(content) <= 0) {
        return;
    }
    const RECT track = ScrollbarTrackRect(content);
    const RECT thumb = ScrollbarThumbRect(content, sceneContext);
    GdiDrawing::DrawSharpFrame(dc, track, RGB(22, 24, 27), RGB(38, 42, 47));
    const bool dragging = sceneContext.Plugins().IsScrollbarDragging();
    GdiDrawing::DrawSharpFrame(dc, thumb, dragging ? RGB(104, 116, 130) : RGB(76, 86, 98), dragging ? RGB(128, 142, 158) : RGB(94, 105, 118));
}

} // namespace

void PluginsPanelRenderer::Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext) const {
    static_cast<void>(theme);
    GdiDrawing::FillRectColor(dc, content, RGB(26, 28, 31));
    DrawHeader(dc, content);
    DrawColumnHeader(dc, content);

    const RECT rows = ListRowsRect(content);
    const int viewportHeight = RectHeight(rows);
    if (viewportHeight <= 0) {
        return;
    }

    const std::int64_t offset = std::clamp(sceneContext.Plugins().ScrollOffset(), std::int64_t{ 0 }, MaxScrollOffset(content));
    const std::size_t firstVisible = static_cast<std::size_t>(offset / kRowHeight);
    const int yRemainder = static_cast<int>(offset % kRowHeight);
    const int visibleRows = (viewportHeight / kRowHeight) + 2;
    const std::size_t lastVisible = std::min<std::size_t>(EditorPluginCatalog::Count(), firstVisible + static_cast<std::size_t>(visibleRows));

    for (std::size_t index = firstVisible; index < lastVisible; ++index) {
        const int rowTop = rows.top + (static_cast<int>(index - firstVisible) * kRowHeight) - yRemainder;
        RECT row{ rows.left, rowTop, rows.right, rowTop + kRowHeight };
        if (row.bottom <= rows.top || row.top >= rows.bottom) {
            continue;
        }
        row.top = std::max(row.top, rows.top);
        row.bottom = std::min(row.bottom, rows.bottom);
        DrawRow(dc, row, index, sceneContext);
    }

    if (EditorPluginCatalog::Count() == 0U) {
        DrawText(dc, rows, "No plugins discovered.", RGB(86, 92, 100), 12, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    DrawScrollbar(dc, content, sceneContext);
}

PluginsPanelRenderer::Hit PluginsPanelRenderer::HitTest(const RECT& content, const EditorSceneContext& sceneContext, int x, int y) {
    const RECT thumb = ScrollbarThumbRect(content, sceneContext);
    if (PointInRect(thumb, x, y)) {
        return Hit{ .kind = PluginsPanelHitKind::ScrollbarThumb, .index = 0, .rect = thumb };
    }
    const RECT track = ScrollbarTrackRect(content);
    if (MaxScrollOffset(content) > 0 && PointInRect(track, x, y)) {
        return Hit{ .kind = PluginsPanelHitKind::ScrollbarTrack, .index = 0, .rect = track };
    }

    const RECT rows = ListRowsRect(content);
    if (!PointInRect(rows, x, y)) {
        return {};
    }

    const std::int64_t offset = std::clamp(sceneContext.Plugins().ScrollOffset(), std::int64_t{ 0 }, MaxScrollOffset(content));
    const std::int64_t rawIndex = (offset + (y - rows.top)) / kRowHeight;
    if (rawIndex < 0 || static_cast<std::size_t>(rawIndex) >= EditorPluginCatalog::Count()) {
        return {};
    }

    const std::size_t index = static_cast<std::size_t>(rawIndex);
    const int rowTop = rows.top + static_cast<int>((rawIndex * kRowHeight) - offset);
    const RECT row{ rows.left, rowTop, rows.right, rowTop + kRowHeight };
    const RECT checkbox{ row.left + 8, row.top + 8, row.left + 24, row.top + 24 };
    if (PointInRect(checkbox, x, y)) {
        return Hit{ .kind = PluginsPanelHitKind::Toggle, .index = index, .rect = checkbox };
    }
    return Hit{ .kind = PluginsPanelHitKind::Row, .index = index, .rect = row };
}

std::int64_t PluginsPanelRenderer::MaxScrollOffset(const RECT& content) noexcept {
    return std::max(std::int64_t{ 0 }, ContentHeight() - RectHeight(ListRowsRect(content)));
}

int PluginsPanelRenderer::ScrollbarTrackTravel(const RECT& content) noexcept {
    const RECT track = ScrollbarTrackRect(content);
    const int viewportHeight = RectHeight(ListRowsRect(content));
    const std::int64_t contentHeight = ContentHeight();
    const int trackHeight = RectHeight(track);
    if (trackHeight <= 0 || contentHeight <= viewportHeight) {
        return 0;
    }
    const int thumbHeight = std::clamp(static_cast<int>((static_cast<std::int64_t>(trackHeight) * viewportHeight) / std::max(std::int64_t{ 1 }, contentHeight)), kScrollbarMinThumb, trackHeight);
    return std::max(0, trackHeight - thumbHeight);
}

} // namespace kb::editor

#endif
