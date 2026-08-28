#include "rendering/components/EditorDialogStyle.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <algorithm>
#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] int Width(const RECT& rect) noexcept {
    return std::max(0L, rect.right - rect.left);
}

[[nodiscard]] int Height(const RECT& rect) noexcept {
    return std::max(0L, rect.bottom - rect.top);
}

[[nodiscard]] RECT Inset(RECT rect, int left, int top, int right, int bottom) noexcept {
    rect.left += left;
    rect.top += top;
    rect.right -= right;
    rect.bottom -= bottom;
    return rect;
}

void FillRound(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int radius) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ previousBrush = SelectObject(dc, brush);
    HGDIOBJ previousPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, previousPen);
    SelectObject(dc, previousBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

[[nodiscard]] RECT ClampRect(RECT rect, const RECT& bounds) noexcept {
    rect.left = std::clamp(rect.left, bounds.left, bounds.right);
    rect.right = std::clamp(rect.right, rect.left, bounds.right);
    rect.top = std::clamp(rect.top, bounds.top, bounds.bottom);
    rect.bottom = std::clamp(rect.bottom, rect.top, bounds.bottom);
    return rect;
}

} // namespace

COLORREF EditorDialogStyle::Color(EditorColor color) noexcept {
    return GdiDrawing::ToColorRef(color);
}

COLORREF EditorDialogStyle::Blend(COLORREF first, COLORREF second, int secondPercent) noexcept {
    const int percent = std::clamp(secondPercent, 0, 100);
    const int firstPercent = 100 - percent;
    return RGB(
        (GetRValue(first) * firstPercent + GetRValue(second) * percent) / 100,
        (GetGValue(first) * firstPercent + GetGValue(second) * percent) / 100,
        (GetBValue(first) * firstPercent + GetBValue(second) * percent) / 100);
}

EditorDialogChromeLayout EditorDialogStyle::ResolveHeader(
    const EditorDialogHeaderDescriptor& descriptor) noexcept {
    const RECT bounds = descriptor.bounds;
    const int titleBottom = std::min<int>(bounds.bottom, bounds.top + TitleBarHeight);
    const RECT titleStrip{bounds.left, bounds.top, bounds.right, titleBottom};
    const RECT close = descriptor.closeButton.right > descriptor.closeButton.left
        ? ClampRect(descriptor.closeButton, titleStrip)
        : RECT{};
    const int tabRightLimit = close.right > close.left
        ? std::max<LONG>(bounds.left, close.left - 6)
        : bounds.right;
    const int tabWidth = std::clamp(Width(bounds) * 52 / 100, 140, 260);
    const RECT activeTab{
        bounds.left,
        bounds.top,
        std::clamp<LONG>(bounds.left + tabWidth, bounds.left, tabRightLimit),
        titleBottom,
    };
    const int iconSize = descriptor.showIcon ? 15 : 0;
    const int iconTop = bounds.top + (Height(titleStrip) - iconSize) / 2;
    const RECT icon = descriptor.showIcon
        ? ClampRect(RECT{activeTab.left + 9, iconTop, activeTab.left + 9 + iconSize, iconTop + iconSize}, activeTab)
        : RECT{};
    const int titleLeft = descriptor.showIcon ? icon.right + 7 : activeTab.left + 10;
    const RECT contextStrip{bounds.left, titleBottom, bounds.right, bounds.bottom};
    return EditorDialogChromeLayout{
        .bounds = bounds,
        .titleStrip = titleStrip,
        .activeTab = activeTab,
        .icon = icon,
        .title = RECT{titleLeft, activeTab.top, std::max<LONG>(titleLeft, activeTab.right - 10), activeTab.bottom},
        .closeButton = close,
        .contextStrip = contextStrip,
        .description = Inset(contextStrip, Padding, 0, Padding, 0),
    };
}

EditorDialogListRowLayout EditorDialogStyle::ResolveListRow(
    const EditorDialogListRowDescriptor& descriptor) noexcept {
    const RECT bounds = descriptor.bounds;
    const int markerWidth = descriptor.selected ? 3 : 0;
    const int contentLeft = std::min<int>(bounds.right, bounds.left + 10 + markerWidth);
    const int iconSize = descriptor.showIcon ? 16 : 0;
    const int iconTop = bounds.top + (Height(bounds) - iconSize) / 2;
    const RECT icon = descriptor.showIcon
        ? ClampRect(RECT{contentLeft, iconTop, contentLeft + iconSize, iconTop + iconSize}, bounds)
        : RECT{};
    const int textLeft = descriptor.showIcon ? icon.right + 8 : contentLeft;
    const int textRight = std::max<int>(textLeft, bounds.right - 10);
    const int contentWidth = std::max(0, textRight - textLeft);
    const int split = descriptor.subtitle.empty()
        ? textRight
        : std::clamp(textLeft + contentWidth * 42 / 100, textLeft, textRight);
    return EditorDialogListRowLayout{
        .bounds = bounds,
        .selectionMarker = RECT{bounds.left, bounds.top, bounds.left + markerWidth, bounds.bottom},
        .icon = icon,
        .title = RECT{textLeft, bounds.top, split, bounds.bottom},
        .subtitle = RECT{std::min(textRight, split + 10), bounds.top, textRight, bounds.bottom},
    };
}

void EditorDialogStyle::PaintSurface(HDC dc, const RECT& bounds, const EditorTheme& theme) {
    GdiDrawing::DrawSharpFrame(dc, bounds, Color(theme.panel), Color(theme.borderPanel));
}

void EditorDialogStyle::PaintTitleBar(
    HDC dc,
    const EditorTheme& theme,
    const EditorDialogHeaderDescriptor& descriptor) {
    const EditorDialogChromeLayout layout = ResolveHeader(descriptor);
    GdiDrawing::FillRectColor(dc, layout.titleStrip, Color(theme.strip));
    GdiDrawing::FillRectColor(dc, layout.activeTab, Color(theme.tabActive));
    PaintDivider(dc, RECT{layout.titleStrip.left, layout.titleStrip.bottom - 1, layout.titleStrip.right, layout.titleStrip.bottom}, theme);
    GdiDrawing::FillRectColor(dc, RECT{layout.activeTab.left, layout.activeTab.top, layout.activeTab.right, layout.activeTab.top + 1}, Color(theme.borderPanel));
    GdiDrawing::FillRectColor(dc, RECT{layout.activeTab.right - 1, layout.activeTab.top + 1, layout.activeTab.right, layout.activeTab.bottom}, Color(theme.borderPanel));
    if (descriptor.showIcon) {
        HeroIconPainter::Draw(dc, layout.icon, descriptor.icon, Color(theme.textPrimary), 1);
    }
    PaintText(dc, layout.title, descriptor.title, Color(theme.textPrimary), 12, FW_SEMIBOLD);
    if (layout.closeButton.right > layout.closeButton.left) {
        GdiDrawing::FillRectColor(
            dc,
            layout.closeButton,
            Color(descriptor.closeHovered ? theme.toolbarButton : theme.strip));
        HeroIconPainter::Draw(
            dc,
            Inset(layout.closeButton, 6, 6, 6, 6),
            HeroIconKind::XMark,
            Color(descriptor.closeHovered ? theme.textPrimary : theme.textSecondary),
            1);
    }
}

void EditorDialogStyle::PaintHeader(
    HDC dc,
    const EditorTheme& theme,
    const EditorDialogHeaderDescriptor& descriptor) {
    PaintTitleBar(dc, theme, descriptor);
    const EditorDialogChromeLayout layout = ResolveHeader(descriptor);
    if (layout.contextStrip.bottom <= layout.contextStrip.top) return;
    PaintToolbar(dc, layout.contextStrip, theme);
    GdiDrawing::FillRectColor(
        dc,
        RECT{layout.contextStrip.left + Padding, layout.contextStrip.top + 9,
             layout.contextStrip.left + Padding + 2, layout.contextStrip.bottom - 9},
        Color(theme.accent));
    RECT description = layout.description;
    description.left += 10;
    PaintText(dc, description, descriptor.description, Color(theme.textSecondary), 11);
}

void EditorDialogStyle::PaintToolbar(HDC dc, const RECT& bounds, const EditorTheme& theme) {
    GdiDrawing::FillRectColor(dc, bounds, Color(theme.chrome));
    PaintDivider(dc, RECT{bounds.left, bounds.bottom - 1, bounds.right, bounds.bottom}, theme);
}

void EditorDialogStyle::PaintFooter(HDC dc, const RECT& bounds, const EditorTheme& theme) {
    GdiDrawing::FillRectColor(dc, bounds, Color(theme.strip));
    PaintDivider(dc, RECT{bounds.left, bounds.top, bounds.right, bounds.top + 1}, theme);
}

void EditorDialogStyle::PaintButton(
    HDC dc,
    const RECT& bounds,
    const EditorTheme& theme,
    std::string_view label,
    EditorDialogButtonTone tone,
    bool hovered,
    bool enabled) {
    // The primary action carries its weight by being filled, the way the rest of the
    // shell marks a chosen state. It used to be a neutral button with an accent bar
    // ruled along its bottom edge, which read as a stray underline rather than a
    // default action.
    const COLORREF accent = Color(theme.accent);
    COLORREF fill = Color(theme.chrome);
    COLORREF border = Color(theme.borderPanel);
    COLORREF ink = Color(theme.textPrimary);
    if (!enabled) {
        fill = Color(theme.strip);
        border = Color(theme.borderChrome);
        ink = Color(theme.textDisabled);
    } else if (tone == EditorDialogButtonTone::Primary) {
        fill = hovered ? Blend(accent, RGB(255, 255, 255), 14) : accent;
        border = Blend(accent, RGB(255, 255, 255), 22);
        ink = RGB(250, 250, 255);
    } else if (tone == EditorDialogButtonTone::Destructive) {
        fill = hovered ? RGB(72, 39, 42) : RGB(55, 31, 34);
        border = hovered ? RGB(189, 91, 96) : RGB(126, 67, 72);
    } else if (hovered) {
        fill = Color(theme.toolbarButton);
        border = Color(theme.textDisabled);
    }
    FillRound(dc, bounds, fill, border, ButtonRadius);
    PaintText(dc, Inset(bounds, 7, 0, 7, 0), label, ink, 11, FW_SEMIBOLD,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void EditorDialogStyle::PaintField(
    HDC dc,
    const RECT& bounds,
    const EditorTheme& theme,
    std::string_view value,
    bool focused,
    bool placeholder) {
    GdiDrawing::DrawSharpFrame(
        dc,
        bounds,
        Color(theme.background),
        Color(focused ? theme.accent : theme.borderPanel));
    PaintText(dc, Inset(bounds, 8, 0, 6, 0), value,
        Color(placeholder ? theme.textDisabled : theme.textPrimary), 11);
}

void EditorDialogStyle::PaintListFrame(HDC dc, const RECT& bounds, const EditorTheme& theme) {
    GdiDrawing::FillRectColor(dc, bounds, Color(theme.panel));
    PaintDivider(dc, RECT{bounds.left, bounds.top, bounds.right, bounds.top + 1}, theme);
    PaintDivider(dc, RECT{bounds.left, bounds.bottom - 1, bounds.right, bounds.bottom}, theme);
}

void EditorDialogStyle::PaintListRow(
    HDC dc,
    const EditorTheme& theme,
    const EditorDialogListRowDescriptor& descriptor) {
    const EditorDialogListRowLayout layout = ResolveListRow(descriptor);
    const EditorColor fill = descriptor.selected
        ? theme.tabActive
        : (descriptor.hovered ? theme.toolbarButton : theme.panel);
    GdiDrawing::FillRectColor(dc, layout.bounds, Color(fill));
    if (descriptor.selected) {
        GdiDrawing::FillRectColor(dc, layout.selectionMarker, Color(theme.accent));
    }
    PaintDivider(dc, RECT{layout.bounds.left, layout.bounds.bottom - 1, layout.bounds.right, layout.bounds.bottom}, theme);
    const COLORREF primary = Color(descriptor.enabled ? theme.textPrimary : theme.textDisabled);
    const COLORREF secondary = Color(descriptor.enabled ? theme.textSecondary : theme.textDisabled);
    if (descriptor.showIcon) {
        HeroIconPainter::Draw(dc, layout.icon, descriptor.icon, descriptor.selected ? Color(theme.accent) : secondary, 1);
    }
    PaintText(dc, layout.title, descriptor.title, primary, 11, descriptor.selected ? FW_SEMIBOLD : FW_NORMAL);
    PaintText(dc, layout.subtitle, descriptor.subtitle, secondary, 10);
}

void EditorDialogStyle::PaintMenuRow(
    HDC dc,
    const RECT& bounds,
    const EditorTheme& theme,
    std::string_view label,
    HeroIconKind icon,
    bool hovered,
    bool enabled,
    bool showIcon) {
    GdiDrawing::FillRectColor(dc, bounds, Color(hovered ? theme.toolbarButton : theme.panel));
    int textLeft = bounds.left + 9;
    if (showIcon) {
        const int iconTop = bounds.top + (Height(bounds) - 15) / 2;
        const RECT iconRect{bounds.left + 9, iconTop, bounds.left + 24, iconTop + 15};
        HeroIconPainter::Draw(dc, iconRect, icon, Color(enabled ? theme.textSecondary : theme.textDisabled), 1);
        textLeft = iconRect.right + 7;
    }
    PaintText(
        dc,
        RECT{textLeft, bounds.top, bounds.right - 8, bounds.bottom},
        label,
        Color(enabled ? (hovered ? theme.textPrimary : theme.textSecondary) : theme.textDisabled),
        11,
        hovered ? FW_SEMIBOLD : FW_NORMAL);
}

void EditorDialogStyle::PaintCheckbox(
    HDC dc,
    const RECT& bounds,
    const EditorTheme& theme,
    bool checked,
    bool enabled) {
    GdiDrawing::DrawSharpFrame(
        dc,
        bounds,
        Color(checked && enabled ? theme.tabActive : theme.background),
        Color(checked && enabled ? theme.accent : theme.borderPanel));
    if (!checked) return;
    PaintText(dc, bounds, "\xE2\x9C\x93", Color(enabled ? theme.textPrimary : theme.textDisabled), 9, FW_SEMIBOLD,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void EditorDialogStyle::PaintScrollbar(
    HDC dc,
    const RECT& track,
    const RECT& thumb,
    const EditorTheme& theme,
    bool dragging) {
    GdiDrawing::FillRectColor(dc, track, Color(theme.background));
    if (thumb.bottom <= thumb.top || thumb.right <= thumb.left) return;
    GdiDrawing::FillRectColor(dc, thumb, Color(dragging ? theme.accent : theme.textDisabled));
}

void EditorDialogStyle::PaintDivider(HDC dc, const RECT& bounds, const EditorTheme& theme) {
    GdiDrawing::FillRectColor(dc, bounds, Color(theme.borderChrome));
}

void EditorDialogStyle::PaintText(
    HDC dc,
    RECT bounds,
    std::string_view text,
    COLORREF color,
    int pointSize,
    int weight,
    UINT format) {
    if (text.empty() || bounds.right <= bounds.left || bounds.bottom <= bounds.top) return;
    const int wideLength = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (wideLength <= 0) return;
    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
            wide.data(), wideLength) != wideLength) return;
    ScopedFont font{pointSize, weight};
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, wide.data(), wideLength, &bounds, format | DT_NOPREFIX);
}

} // namespace kb::editor
#endif
