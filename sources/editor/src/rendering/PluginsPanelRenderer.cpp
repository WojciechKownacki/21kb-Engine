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

[[nodiscard]] COLORREF Color(EditorColor color) noexcept {
    return GdiDrawing::ToColorRef(color);
}

[[nodiscard]] COLORREF Blend(COLORREF a, COLORREF b, int percentB) noexcept {
    const int percentA = 100 - percentB;
    return RGB(
        (GetRValue(a) * percentA + GetRValue(b) * percentB) / 100,
        (GetGValue(a) * percentA + GetGValue(b) * percentB) / 100,
        (GetBValue(a) * percentA + GetBValue(b) * percentB) / 100);
}

[[nodiscard]] RECT ProviderAddRect(const RECT& content) noexcept {
    return { content.right - 154, content.top + 9, content.right - 86, content.top + 33 };
}

[[nodiscard]] RECT ProviderCancelRect(const RECT& content) noexcept {
    return { content.right - 78, content.top + 9, content.right - 10, content.top + 33 };
}

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

void DrawCheckbox(HDC dc, RECT rect, const EditorTheme& theme, bool enabled) {
    const COLORREF fill = enabled ? Color(theme.accent) : Color(theme.chrome);
    const COLORREF border = enabled ? Color(theme.accent) : Color(theme.borderPanel);
    GdiDrawing::DrawSharpFrame(dc, rect, fill, border);
    if (enabled) {
        DrawText(dc, rect, "x", Color(theme.textPrimary), 12, FW_BOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void DrawHeader(HDC dc, const RECT& content, const EditorTheme& theme) {
    const RECT header = HeaderRect(content);
    GdiDrawing::FillRectColor(dc, header, Color(theme.strip));
    GdiDrawing::FillRectColor(dc, RECT{ header.left, header.top, header.left + 3, header.bottom }, Color(theme.accent));
    GdiDrawing::FillRectColor(dc, RECT{ header.left, header.bottom - 1, header.right, header.bottom }, Color(theme.borderChrome));
    DrawText(dc, RECT{ header.left + kPadding, header.top, header.right - kPadding, header.bottom }, "Plugins", Color(theme.textPrimary), 14, FW_SEMIBOLD);
}

void DrawHeader(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    DrawHeader(dc, content, theme);
    if (sceneContext.HasPendingParticleProviderMigration()) {
        const RECT header = HeaderRect(content);
        DrawText(dc, RECT{ header.left + 92, header.top, header.right - 166, header.bottom },
            "Particle effects detected", RGB(223, 178, 91), 11, FW_NORMAL, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        GdiDrawing::DrawSharpFrame(dc, ProviderAddRect(content), Color(theme.accent), Color(theme.accent));
        GdiDrawing::DrawSharpFrame(dc, ProviderCancelRect(content), Color(theme.chrome), Color(theme.borderPanel));
        DrawText(dc, ProviderAddRect(content), "Add", Color(theme.textPrimary), 11, FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawText(dc, ProviderCancelRect(content), "Cancel", Color(theme.textSecondary), 11, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else if (sceneContext.Plugins().HasPendingReload()) {
        const RECT header = HeaderRect(content);
        DrawText(dc, RECT{ header.left + 110, header.top, header.right - kPadding, header.bottom }, "Pending scene reload", RGB(223, 178, 91), 11, FW_NORMAL, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

void DrawColumnHeader(HDC dc, const RECT& content, const EditorTheme& theme) {
    const RECT header = ColumnHeaderRect(content);
    GdiDrawing::FillRectColor(dc, header, Color(theme.chrome));
    DrawText(dc, RECT{ header.left + kPadding + kPluginColumnLeft, header.top, header.left + kPluginColumnRight, header.bottom }, "PLUGIN", Color(theme.textSecondary), 11, FW_SEMIBOLD);
    DrawText(dc, RECT{ header.left + kPluginColumnRight, header.top, header.left + kStatusColumnRight, header.bottom }, "STATUS", Color(theme.textSecondary), 11, FW_SEMIBOLD);
    DrawText(dc, RECT{ header.left + kStatusColumnRight, header.top, header.left + kCategoryColumnRight, header.bottom }, "CATEGORY", Color(theme.textSecondary), 11, FW_SEMIBOLD);
    DrawText(dc, RECT{ header.left + kCategoryColumnRight, header.top, header.right - kPadding, header.bottom }, "BINARY", Color(theme.textSecondary), 11, FW_SEMIBOLD);
    GdiDrawing::FillRectColor(dc, RECT{ header.left, header.bottom - 1, header.right, header.bottom }, Color(theme.borderChrome));
}

void DrawRow(HDC dc, const RECT& row, const EditorTheme& theme, std::size_t index, const EditorSceneContext& sceneContext) {
    const EditorPluginDescriptor* plugin = EditorPluginCatalog::At(index);
    if (plugin == nullptr) {
        return;
    }

    const bool hovered = sceneContext.Plugins().HoveredPluginIndex() == index;
    const bool enabled = sceneContext.IsProjectPluginEnabled(plugin->id);
    const bool pendingReload = sceneContext.Plugins().HasPendingReload();
    const COLORREF rowFill = (index & 1U) != 0U ? Blend(Color(theme.panel), Color(theme.strip), 24) : Color(theme.panel);
    GdiDrawing::FillRectColor(dc, row, hovered ? Blend(Color(theme.panel), Color(theme.accent), 9) : rowFill);
    if (enabled) {
        GdiDrawing::FillRectColor(dc, RECT{ row.left, row.top, row.left + 3, row.bottom }, Color(theme.accent));
    }

    const RECT checkbox{ row.left + 8, row.top + 8, row.left + 24, row.top + 24 };
    DrawCheckbox(dc, checkbox, theme, enabled);
    DrawText(dc, RECT{ row.left + 34, row.top + 1, row.left + kPluginColumnRight - 10, row.top + 18 }, plugin->displayName.data(), Color(theme.textPrimary), 12, FW_SEMIBOLD);
    DrawText(dc, RECT{ row.left + 34, row.top + 17, row.left + kPluginColumnRight - 10, row.bottom }, plugin->id.data(), Color(theme.textDisabled), 11);
    const char* status = enabled ? (pendingReload ? "Enabled*" : "Enabled") : (pendingReload ? "Disabled*" : "Disabled");
    DrawText(dc, RECT{ row.left + kPluginColumnRight, row.top, row.left + kStatusColumnRight, row.bottom }, status, pendingReload ? RGB(223, 178, 91) : (enabled ? RGB(126, 201, 143) : RGB(136, 145, 156)), 12);
    DrawText(dc, RECT{ row.left + kStatusColumnRight, row.top, row.left + kCategoryColumnRight, row.bottom }, plugin->category.data(), Color(theme.textSecondary), 12);

    const std::string binary = sceneContext.ProjectPluginBinaryPath(plugin->id).empty()
        ? std::string{ plugin->binaryPath }
        : sceneContext.ProjectPluginBinaryPath(plugin->id);
    DrawText(dc, RECT{ row.left + kCategoryColumnRight, row.top, row.right - 10, row.bottom }, binary.c_str(), Color(theme.textDisabled), 11);
    GdiDrawing::FillRectColor(dc, RECT{ row.left, row.bottom - 1, row.right, row.bottom }, Color(theme.borderChrome));
}

void DrawScrollbar(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    if (PluginsPanelRenderer::MaxScrollOffset(content) <= 0) {
        return;
    }
    const RECT track = ScrollbarTrackRect(content);
    const RECT thumb = ScrollbarThumbRect(content, sceneContext);
    GdiDrawing::DrawSharpFrame(dc, track, Color(theme.chrome), Color(theme.borderChrome));
    const bool dragging = sceneContext.Plugins().IsScrollbarDragging();
    GdiDrawing::DrawSharpFrame(dc, thumb, Color(dragging ? theme.accent : theme.borderPanel), Color(dragging ? theme.textSecondary : theme.borderPanel));
}

} // namespace

void PluginsPanelRenderer::Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext) const {
    GdiDrawing::FillRectColor(dc, content, Color(theme.panel));
    DrawHeader(dc, content, theme, sceneContext);
    DrawColumnHeader(dc, content, theme);

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
        DrawRow(dc, row, theme, index, sceneContext);
    }

    if (EditorPluginCatalog::Count() == 0U) {
        DrawText(dc, rows, "No plugins discovered.", Color(theme.textDisabled), 12, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    DrawScrollbar(dc, content, theme, sceneContext);
}

PluginsPanelRenderer::Hit PluginsPanelRenderer::HitTest(const RECT& content, const EditorSceneContext& sceneContext, int x, int y) {
    if (sceneContext.HasPendingParticleProviderMigration()) {
        const RECT add = ProviderAddRect(content);
        if (PointInRect(add, x, y)) return { .kind = PluginsPanelHitKind::ParticleProviderAdd, .rect = add };
        const RECT cancel = ProviderCancelRect(content);
        if (PointInRect(cancel, x, y)) return { .kind = PluginsPanelHitKind::ParticleProviderCancel, .rect = cancel };
    }
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
