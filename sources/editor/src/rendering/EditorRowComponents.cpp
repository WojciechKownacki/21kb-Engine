#include "rendering/components/CategoryHeader.hpp"
#include "rendering/components/DenseListRow.hpp"
#include "rendering/components/PropertyRow.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/ProjectFilesPanelDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <algorithm>
#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] COLORREF Color(EditorColor color) noexcept {
    return GdiDrawing::ToColorRef(color);
}

[[nodiscard]] RECT Inset(RECT rect, int left, int top, int right, int bottom) noexcept {
    rect.left += left;
    rect.top += top;
    rect.right -= right;
    rect.bottom -= bottom;
    return rect;
}

void Text(
    HDC dc,
    RECT rect,
    std::string_view text,
    COLORREF color,
    int pointSize,
    int weight,
    UINT format) {
    if (text.empty() || rect.right <= rect.left || rect.bottom <= rect.top) return;
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
    DrawTextW(dc, wide.data(), wideLength, &rect, format | DT_NOPREFIX);
}

} // namespace

void CategoryHeader::Paint(
    HDC dc,
    const EditorTheme& theme,
    const CategoryHeaderDescriptor& descriptor) {
    const CategoryHeaderLayout layout = Resolve(
        descriptor.bounds,
        descriptor.showIcon,
        descriptor.showTrailingAction,
        !descriptor.trailingText.empty());
    const COLORREF background = Color(descriptor.hovered ? theme.toolbarButton : theme.strip);
    const COLORREF primary = Color(descriptor.enabled ? theme.textPrimary : theme.textDisabled);
    const COLORREF secondary = Color(descriptor.enabled ? theme.textSecondary : theme.textDisabled);
    GdiDrawing::FillRectColor(dc, layout.bounds, background);
    GdiDrawing::FillRectColor(
        dc,
        RECT{layout.bounds.left, layout.bounds.bottom - 1, layout.bounds.right, layout.bounds.bottom},
        Color(theme.borderChrome));
    ProjectFilesPanelDrawing::DrawDisclosureTriangle(
        dc, Inset(layout.disclosure, 4, 4, 4, 4), secondary, descriptor.expanded);
    if (descriptor.showIcon) {
        HeroIconPainter::Draw(dc, layout.icon, descriptor.icon, secondary, 2);
    }
    Text(dc, layout.title, descriptor.title, primary, 12, FW_SEMIBOLD,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    Text(dc, layout.trailingText, descriptor.trailingText, secondary, 11, FW_NORMAL,
        DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (descriptor.showTrailingAction) {
        GdiDrawing::DrawSharpFrame(
            dc,
            layout.trailingAction,
            Color(descriptor.trailingActionHovered ? theme.toolbarButton : theme.strip),
            Color(theme.borderPanel));
        HeroIconPainter::Draw(
            dc, Inset(layout.trailingAction, 4, 4, 4, 4), descriptor.trailingActionIcon, secondary, 1);
    }
}

void DenseListRow::Paint(
    HDC dc,
    const EditorTheme& theme,
    const DenseListRowDescriptor& descriptor) {
    const DenseListRowLayout layout = Resolve(
        descriptor.bounds,
        descriptor.contentLeftInset,
        descriptor.contentRightInset,
        descriptor.showIcon);
    const COLORREF fill = descriptor.selected
        ? ProjectFilesPanelDrawing::Blend(Color(theme.panel), Color(theme.accent), 16)
        : Color(descriptor.hovered ? theme.toolbarButton : theme.panel);
    GdiDrawing::FillRectColor(dc, layout.bounds, fill);
    if (descriptor.selected) {
        GdiDrawing::FillRectColor(
            dc,
            RECT{layout.bounds.left, layout.bounds.top, layout.bounds.left + 3, layout.bounds.bottom},
            Color(theme.accent));
    }
    if (descriptor.showDivider) {
        GdiDrawing::FillRectColor(
            dc,
            RECT{layout.bounds.left, layout.bounds.bottom - 1, layout.bounds.right, layout.bounds.bottom},
            Color(theme.borderChrome));
    }
    const COLORREF ink = Color(descriptor.enabled ? theme.textPrimary : theme.textDisabled);
    if (descriptor.showIcon) {
        HeroIconPainter::Draw(dc, layout.icon, descriptor.icon, Color(descriptor.selected ? theme.accent : (descriptor.enabled ? theme.textSecondary : theme.textDisabled)), 2);
    }
    if (descriptor.summary.empty()) {
        Text(dc, layout.text, descriptor.title, ink, 12, descriptor.selected ? FW_SEMIBOLD : FW_NORMAL,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }
    const int textWidth = std::max(0, static_cast<int>(layout.text.right - layout.text.left));
    const int titleWidth = std::clamp(textWidth * 44 / 100, 0, textWidth);
    RECT title = layout.text;
    title.right = title.left + titleWidth;
    RECT summary = layout.text;
    summary.left = std::min(summary.right, title.right + 8);
    Text(dc, title, descriptor.title, ink, 12, FW_SEMIBOLD,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    Text(dc, summary, descriptor.summary, Color(descriptor.enabled ? theme.textSecondary : theme.textDisabled), 11, FW_NORMAL,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void PropertyRow::PaintBackground(
    HDC dc,
    const EditorTheme& theme,
    const RECT& bounds,
    bool hovered) {
    GdiDrawing::FillRectColor(dc, bounds, Color(hovered ? theme.toolbarButton : theme.panel));
}

void PropertyRow::PaintLabel(
    HDC dc,
    const EditorTheme& theme,
    RECT bounds,
    std::string_view label,
    bool enabled,
    bool topAligned) {
    Text(dc, bounds, label, Color(enabled ? theme.textSecondary : theme.textDisabled), 12, FW_SEMIBOLD,
        DT_LEFT | (topAligned ? DT_TOP : DT_VCENTER) | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void PropertyRow::PaintValue(
    HDC dc,
    const EditorTheme& theme,
    RECT bounds,
    std::string_view value,
    bool hovered,
    bool enabled,
    PropertyRowValueAlignment alignment) {
    GdiDrawing::DrawSharpFrame(
        dc,
        bounds,
        Color(hovered ? theme.toolbarButton : theme.chrome),
        Color(theme.borderPanel));
    bounds = Inset(bounds, 10, 0, 4, 0);
    UINT textAlignment = DT_LEFT;
    if (alignment == PropertyRowValueAlignment::Center) textAlignment = DT_CENTER;
    else if (alignment == PropertyRowValueAlignment::Right) textAlignment = DT_RIGHT;
    Text(dc, bounds, value, Color(enabled ? theme.textPrimary : theme.textDisabled), 12, FW_NORMAL,
        textAlignment | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void PropertyRow::Paint(
    HDC dc,
    const EditorTheme& theme,
    const PropertyRowDescriptor& descriptor) {
    const PropertyRowLayout layout = Resolve(descriptor.bounds);
    PaintBackground(dc, theme, layout.bounds, descriptor.hovered);
    PaintLabel(dc, theme, layout.label, descriptor.label, descriptor.enabled);
    PaintValue(
        dc,
        theme,
        layout.value,
        descriptor.value,
        descriptor.valueHovered,
        descriptor.enabled && descriptor.editable,
        descriptor.valueAlignment);
}

} // namespace kb::editor
#endif
