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

namespace {

[[nodiscard]] RECT FitAspectRatio(RECT bounds, int widthRatio, int heightRatio) noexcept {
    const int width = ProjectFilesPanelDrawing::RectWidth(bounds);
    const int height = ProjectFilesPanelDrawing::RectHeight(bounds);
    if (width <= 0 || height <= 0 || widthRatio <= 0 || heightRatio <= 0) {
        return bounds;
    }

    int fittedWidth = width;
    int fittedHeight = (width * heightRatio) / widthRatio;
    if (fittedHeight > height) {
        fittedHeight = height;
        fittedWidth = (height * widthRatio) / heightRatio;
    }

    const int left = bounds.left + (width - fittedWidth) / 2;
    const int top = bounds.top + (height - fittedHeight) / 2;
    return RECT{ left, top, left + fittedWidth, top + fittedHeight };
}

void DrawFastFolder(HDC dc, RECT icon, COLORREF color) {
    if (icon.right <= icon.left || icon.bottom <= icon.top) {
        return;
    }
    const int width = ProjectFilesPanelDrawing::RectWidth(icon);
    const int height = ProjectFilesPanelDrawing::RectHeight(icon);
    const int tabHeight = std::max(3, height / 4);
    const int tabWidth = std::max(8, width / 2);
    const COLORREF border = ProjectFilesPanelDrawing::Blend(color, RGB(60, 42, 10), 38);
    ScopedBrush brush{ color };
    ScopedPen pen{ 1, border };
    const ScopedGdiObject selectedBrush(dc, brush.handle);
    const ScopedGdiObject selectedPen(dc, pen.handle);

    RECT tab{ icon.left + 1, icon.top + 1, icon.left + tabWidth, icon.top + tabHeight + 2 };
    RoundRect(dc, tab.left, tab.top, tab.right, tab.bottom + 2, 5, 5);
    RECT body{ icon.left + 1, icon.top + tabHeight, icon.right - 1, icon.bottom - 1 };
    RoundRect(dc, body.left, body.top, body.right, body.bottom, 6, 6);

    GdiDrawing::FillRectColor(dc, RECT{ body.left + 2, body.top + 2, body.right - 2, body.top + 3 }, ProjectFilesPanelDrawing::Blend(color, RGB(255, 242, 174), 18));
}

void DrawFastCube(HDC dc, RECT icon, COLORREF color) {
    if (icon.right <= icon.left || icon.bottom <= icon.top) {
        return;
    }
    const int cx = (icon.left + icon.right) / 2;
    const int top = icon.top + 1;
    const int midY = icon.top + ProjectFilesPanelDrawing::RectHeight(icon) / 3;
    const int bottom = icon.bottom - 1;
    const int left = icon.left + 1;
    const int right = icon.right - 1;
    const COLORREF topColor = ProjectFilesPanelDrawing::Blend(color, RGB(255, 255, 255), 24);
    const COLORREF leftColor = ProjectFilesPanelDrawing::Blend(color, RGB(0, 0, 0), 10);
    const COLORREF rightColor = ProjectFilesPanelDrawing::Blend(color, RGB(0, 0, 0), 24);

    POINT topFace[4]{ POINT{ cx, top }, POINT{ right, midY }, POINT{ cx, midY + (midY - top) }, POINT{ left, midY } };
    POINT leftFace[4]{ POINT{ left, midY }, POINT{ cx, midY + (midY - top) }, POINT{ cx, bottom }, POINT{ left, bottom - (midY - top) } };
    POINT rightFace[4]{ POINT{ right, midY }, POINT{ cx, midY + (midY - top) }, POINT{ cx, bottom }, POINT{ right, bottom - (midY - top) } };

    ScopedPen pen{ 1, ProjectFilesPanelDrawing::Blend(color, RGB(18, 24, 34), 35) };
    const ScopedGdiObject selectedPen(dc, pen.handle);
    {
        ScopedBrush brush{ topColor };
        const ScopedGdiObject selectedBrush(dc, brush.handle);
        Polygon(dc, topFace, 4);
    }
    {
        ScopedBrush brush{ leftColor };
        const ScopedGdiObject selectedBrush(dc, brush.handle);
        Polygon(dc, leftFace, 4);
    }
    {
        ScopedBrush brush{ rightColor };
        const ScopedGdiObject selectedBrush(dc, brush.handle);
        Polygon(dc, rightFace, 4);
    }
}

} // namespace

void ProjectFilesPanelDrawing::DrawIconWithShadow(HDC dc, RECT icon, HeroIconKind kind, COLORREF color, int strokeWidth) {
    if (kind == HeroIconKind::Folder || kind == HeroIconKind::Cube) {
        const RECT fitted = kind == HeroIconKind::Folder ? FitAspectRatio(icon, 5, 4) : FitAspectRatio(icon, 1, 1);
        RECT shadow = fitted;
        OffsetRect(&shadow, 1, 1);
        if (kind == HeroIconKind::Folder) {
            DrawFastFolder(dc, shadow, RGB(4, 5, 7));
            DrawFastFolder(dc, fitted, color);
        } else {
            DrawFastCube(dc, shadow, RGB(4, 5, 7));
            DrawFastCube(dc, fitted, color);
        }
        return;
    }
    RECT shadow = icon;
    OffsetRect(&shadow, 1, 1);
    HeroIconPainter::Draw(dc, shadow, kind, RGB(4, 5, 7), strokeWidth);
    HeroIconPainter::Draw(dc, icon, kind, color, strokeWidth);
}

} // namespace kb::editor

#endif
