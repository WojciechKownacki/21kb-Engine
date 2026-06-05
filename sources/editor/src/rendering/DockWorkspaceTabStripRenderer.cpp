#include "rendering/DockWorkspaceTabStripRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <algorithm>
#include <cstring>

namespace kb::editor {
namespace {

constexpr int kTabFontSize = 13;
constexpr int kTabTextVerticalOffset = 1;
constexpr int kTabIconSize = 15;
constexpr int kTabIconGap = 6;
constexpr int kTabHorizontalPadding = 9;

void DrawHairline(HDC dc, const RECT& rect, COLORREF color) {
    GdiDrawing::FillRectColor(dc, rect, color);
}

void DrawTabChrome(HDC dc, const RECT& tab, bool active, const EditorTheme& theme) {
    const COLORREF fill = GdiDrawing::ToColorRef(active ? theme.tabActive : theme.strip);
    GdiDrawing::FillRectColor(dc, tab, fill);

    const COLORREF edge = GdiDrawing::ToColorRef(active ? theme.borderPanel : theme.chrome);
    if (active) {
        DrawHairline(dc, RECT{ tab.left, tab.top, tab.right, tab.top + 1 }, edge);
        DrawHairline(dc, RECT{ tab.left, tab.top, tab.left + 1, tab.bottom }, edge);
    }
    DrawHairline(dc, RECT{ tab.right - 1, tab.top + 1, tab.right, tab.bottom - 1 }, edge);
}

[[nodiscard]] RECT TabTitleRect(const DockRect& tab) {
    RECT titleRect = GdiDrawing::ToRect(tab);
    titleRect.left += 8;
    titleRect.right -= 8;
    titleRect.top += kTabTextVerticalOffset;
    return titleRect;
}

[[nodiscard]] bool TabIconForPanel(DockPanelKind kind, HeroIconKind& icon) noexcept {
    switch (kind) {
    case DockPanelKind::Hierarchy:
        icon = HeroIconKind::ListBullet;
        return true;
    case DockPanelKind::Scene:
        icon = HeroIconKind::Cube;
        return true;
    case DockPanelKind::Inspector:
        icon = HeroIconKind::AdjustmentsHorizontal;
        return true;
    case DockPanelKind::Assets:
        icon = HeroIconKind::Folder;
        return true;
    case DockPanelKind::Console:
        icon = HeroIconKind::CommandLine;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] int MeasureTextWidth(HDC dc, const char* text) noexcept {
    SIZE size{};
    return GetTextExtentPoint32A(dc, text, static_cast<int>(std::strlen(text)), &size) ? size.cx : 0;
}

void DrawTabLabel(HDC dc, RECT rect, const char* text, COLORREF color) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void DrawTabContent(HDC dc, const DockPanelLayout& panelLayout, const DockPanel& panel, const EditorTheme& theme) {
    HeroIconKind icon{};
    if (!TabIconForPanel(panel.kind, icon)) {
        RECT titleRect = TabTitleRect(panelLayout.tab);
        GdiDrawing::DrawTabText(dc, titleRect, panel.title.c_str(), GdiDrawing::ToColorRef(panelLayout.active ? theme.textPrimary : theme.textSecondary));
        return;
    }

    const RECT tab = GdiDrawing::ToRect(panelLayout.tab);
    const int tabWidth = std::max(0, static_cast<int>(tab.right - tab.left));
    const int textWidth = MeasureTextWidth(dc, panel.title.c_str());
    const int maxTextWidth = std::max(0, tabWidth - (kTabHorizontalPadding * 2) - kTabIconSize - kTabIconGap);
    const int visibleTextWidth = std::min(textWidth, maxTextWidth);
    const int groupWidth = kTabIconSize + kTabIconGap + visibleTextWidth;
    const int groupLeft = tab.left + std::max(kTabHorizontalPadding, (tabWidth - groupWidth) / 2);
    const int iconTop = tab.top + ((tab.bottom - tab.top - kTabIconSize) / 2) + kTabTextVerticalOffset;

    const COLORREF color = GdiDrawing::ToColorRef(panelLayout.active ? theme.textPrimary : theme.textSecondary);
    const RECT iconRect{ groupLeft, iconTop, groupLeft + kTabIconSize, iconTop + kTabIconSize };
    HeroIconPainter::Draw(dc, iconRect, icon, color, icon == HeroIconKind::Gamepad2 ? 2 : 1);

    RECT textRect{ iconRect.right + kTabIconGap, tab.top + kTabTextVerticalOffset, tab.right - kTabHorizontalPadding, tab.bottom };
    DrawTabLabel(dc, textRect, panel.title.c_str(), color);
}

} // namespace

void DockWorkspaceTabStripRenderer::Paint(HDC dc, const DockLayout& layout, const EditorDockModel& dockModel, const EditorTheme& theme) const {
    ScopedFont titleFont(kTabFontSize, FW_NORMAL);
    const ScopedGdiObject selectedFont(dc, titleFont.handle);

    for (const DockPanelLayout& panelLayout : layout.panels) {
        const DockPanel* panel = dockModel.Queries().FindPanel(panelLayout.panelId);
        if (panel == nullptr) {
            continue;
        }

        DrawTabChrome(dc, GdiDrawing::ToRect(panelLayout.tab), panelLayout.active, theme);
        DrawTabContent(dc, panelLayout, *panel, theme);
    }
}

} // namespace kb::editor

#endif
