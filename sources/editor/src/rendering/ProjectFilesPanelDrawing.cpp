#include "rendering/ProjectFilesPanelDrawing.hpp"

#if defined(_WIN32)
#include "engine/assets/AssetMetadata.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/gdi/ScopedBrush.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "rendering/gdi/ScopedPen.hpp"

#include <string>

namespace kb::editor {

COLORREF ProjectFilesPanelDrawing::Color(EditorColor color) {
    return GdiDrawing::ToColorRef(color);
}

int ProjectFilesPanelDrawing::RectWidth(const RECT& rect) noexcept {
    return static_cast<int>(rect.right - rect.left);
}

int ProjectFilesPanelDrawing::RectHeight(const RECT& rect) noexcept {
    return static_cast<int>(rect.bottom - rect.top);
}

RECT ProjectFilesPanelDrawing::Inset(RECT rect, int x, int y) noexcept {
    rect.left += x;
    rect.right -= x;
    rect.top += y;
    rect.bottom -= y;
    return rect;
}

COLORREF ProjectFilesPanelDrawing::Blend(COLORREF a, COLORREF b, int percentB) noexcept {
    const int percentA = 100 - percentB;
    return RGB(
        (GetRValue(a) * percentA + GetRValue(b) * percentB) / 100,
        (GetGValue(a) * percentA + GetGValue(b) * percentB) / 100,
        (GetBValue(a) * percentA + GetBValue(b) * percentB) / 100);
}

COLORREF ProjectFilesPanelDrawing::FolderColor(bool selected) noexcept {
    return selected ? RGB(247, 196, 70) : RGB(232, 181, 56);
}

bool ProjectFilesPanelDrawing::SameVirtualPath(const std::filesystem::path& left, const std::filesystem::path& right) {
    return kb::assets::NormalizeAssetPath(left) == kb::assets::NormalizeAssetPath(right);
}

void ProjectFilesPanelDrawing::DrawLabel(HDC dc, RECT rect, const char* text, COLORREF color) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void ProjectFilesPanelDrawing::DrawCenteredLabel(HDC dc, RECT rect, const char* text, COLORREF color) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void ProjectFilesPanelDrawing::DrawTextWithFont(HDC dc, RECT rect, const char* text, COLORREF color, int pointSize, int weight, UINT flags) {
    ScopedFont font{ pointSize, weight };
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, static_cast<int>(flags | DT_NOPREFIX));
}

void ProjectFilesPanelDrawing::DrawHairline(HDC dc, RECT rect, COLORREF color) {
    rect.bottom = rect.top + 1;
    GdiDrawing::FillRectColor(dc, rect, color);
}

void ProjectFilesPanelDrawing::DrawEditField(HDC dc, RECT rect, const EditorTheme& theme, std::string_view value) {
    GdiDrawing::DrawSharpFrame(dc, rect, RGB(18, 20, 24), Blend(Color(theme.borderPanel), Color(theme.textSecondary), 34));
    DrawTextWithFont(dc, Inset(rect, 6, 0), std::string{ value }.c_str(), Color(theme.textPrimary), std::clamp(RectHeight(rect) - 5, 8, 12), FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void ProjectFilesPanelDrawing::DrawCenteredEditField(HDC dc, RECT rect, const EditorTheme& theme, std::string_view value) {
    GdiDrawing::DrawSharpFrame(dc, rect, RGB(18, 20, 24), Blend(Color(theme.borderPanel), Color(theme.textSecondary), 34));
    DrawTextWithFont(dc, Inset(rect, 5, 0), std::string{ value }.c_str(), Color(theme.textPrimary), std::clamp(RectHeight(rect) - 5, 8, 12), FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void ProjectFilesPanelDrawing::DrawIconButton(HDC dc, RECT rect, const EditorTheme& theme, HeroIconKind icon, bool active) {
    const COLORREF fill = active ? Blend(Color(theme.tabActive), Color(theme.accent), 10) : Blend(Color(theme.panel), Color(theme.strip), 20);
    const COLORREF border = active ? Blend(Color(theme.accent), Color(theme.borderPanel), 28) : Color(theme.borderPanel);
    GdiDrawing::DrawSharpFrame(dc, rect, fill, border);
    RECT iconRect{ rect.left + 6, rect.top + 4, rect.right - 6, rect.bottom - 4 };
    HeroIconPainter::Draw(dc, iconRect, icon, active ? Color(theme.textPrimary) : Color(theme.textSecondary), 2);
}

void ProjectFilesPanelDrawing::DrawTextButton(HDC dc, RECT rect, const EditorTheme& theme, const char* text, bool active) {
    const COLORREF fill = active ? Blend(Color(theme.tabActive), Color(theme.accent), 10) : Blend(Color(theme.panel), Color(theme.strip), 20);
    const COLORREF border = active ? Blend(Color(theme.accent), Color(theme.borderPanel), 28) : Color(theme.borderPanel);
    GdiDrawing::DrawSharpFrame(dc, rect, fill, border);
    DrawCenteredLabel(dc, Inset(rect, 6, 0), text, active ? Color(theme.textPrimary) : Color(theme.textSecondary));
}

void ProjectFilesPanelDrawing::DrawDisclosureTriangle(HDC dc, RECT rect, COLORREF color, bool expanded) {
    const int centerY = (rect.top + rect.bottom) / 2;
    const int centerX = (rect.left + rect.right) / 2;
    POINT points[3]{};
    if (expanded) {
        points[0] = POINT{ rect.left + 3, centerY - 3 };
        points[1] = POINT{ rect.right - 3, centerY - 3 };
        points[2] = POINT{ centerX, centerY + 4 };
    } else {
        points[0] = POINT{ centerX - 2, rect.top + 5 };
        points[1] = POINT{ centerX - 2, rect.bottom - 5 };
        points[2] = POINT{ centerX + 5, centerY };
    }

    ScopedBrush brush{ color };
    ScopedPen pen{ 1, color };
    const ScopedGdiObject selectedBrush(dc, brush.handle);
    const ScopedGdiObject selectedPen(dc, pen.handle);
    Polygon(dc, points, 3);
}

void ProjectFilesPanelDrawing::DrawIconWithShadow(HDC dc, RECT icon, HeroIconKind kind, COLORREF color, int strokeWidth) {
    RECT shadow = icon;
    OffsetRect(&shadow, 1, 1);
    HeroIconPainter::Draw(dc, shadow, kind, RGB(4, 5, 7), strokeWidth);
    HeroIconPainter::Draw(dc, icon, kind, color, strokeWidth);
}

} // namespace kb::editor

#endif
