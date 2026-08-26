#include "rendering/DockPanelChromeRenderer.hpp"

#if defined(_WIN32)
#include "rendering/DockPanelFramePainter.hpp"
#include "rendering/EditorSurfacePainter.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <algorithm>
namespace kb::editor {
namespace {

constexpr int kTabFontSize = 12;
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

    const COLORREF edge = GdiDrawing::ToColorRef(active ? theme.borderPanel : theme.borderChrome);
    if (active) {
        DrawHairline(dc, RECT{ tab.left, tab.bottom - 2, tab.right, tab.bottom }, GdiDrawing::ToColorRef(theme.accent));
    }
    DrawHairline(dc, RECT{ tab.right - 1, tab.top + 1, tab.right, tab.bottom - 1 }, edge);
}

[[nodiscard]] RECT TabTitleRect(const RECT& tab) {
    RECT titleRect{ tab.left + 8, tab.top + kTabTextVerticalOffset, tab.right - 8, tab.bottom };
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
    case DockPanelKind::ProjectSettings:
        icon = HeroIconKind::Gamepad2;
        return true;
    case DockPanelKind::EditorSettings:
        icon = HeroIconKind::AdjustmentsHorizontal;
        return true;
    case DockPanelKind::ScriptEditor:
        icon = HeroIconKind::CommandLine;
        return true;
    case DockPanelKind::Plugins:
        icon = HeroIconKind::AdjustmentsHorizontal;
        return true;
    case DockPanelKind::MaterialEditor:
        icon = HeroIconKind::RectangleGroup;
        return true;
    case DockPanelKind::ParticleEditor:
        icon = HeroIconKind::Bolt;
        return true;
    default:
        return false;
    }
}

void DrawTabLabel(HDC dc, RECT rect, const char* text, COLORREF color) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void DrawTabContent(HDC dc, const RECT& tab, const DockPanel& panel, const EditorTheme& theme, bool active) {
    HeroIconKind icon{};
    if (!TabIconForPanel(panel.kind, icon)) {
        RECT titleRect = TabTitleRect(tab);
        GdiDrawing::DrawTabText(dc, titleRect, panel.title.c_str(), GdiDrawing::ToColorRef(active ? theme.textPrimary : theme.textSecondary));
        return;
    }

    const int groupLeft = tab.left + kTabHorizontalPadding;
    const int iconTop = tab.top + ((tab.bottom - tab.top - kTabIconSize) / 2) + kTabTextVerticalOffset;

    const COLORREF textColor = GdiDrawing::ToColorRef(active ? theme.textPrimary : theme.textSecondary);
    const COLORREF iconColor = GdiDrawing::ToColorRef(active ? theme.accent : theme.textDisabled);
    const RECT iconRect{ groupLeft, iconTop, groupLeft + kTabIconSize, iconTop + kTabIconSize };
    HeroIconPainter::Draw(dc, iconRect, icon, iconColor, icon == HeroIconKind::Gamepad2 ? 2 : 1);

    RECT textRect{ iconRect.right + kTabIconGap, tab.top + kTabTextVerticalOffset, tab.right - kTabHorizontalPadding, tab.bottom };
    DrawTabLabel(dc, textRect, panel.title.c_str(), textColor);
}

} // namespace

void DockPanelChromeRenderer::Paint(HDC dc, const RECT& rect, const DockPanel& panel, const EditorTheme& theme, const EditorMetrics& metrics, bool active) const {
    ScopedFont titleFont(kTabFontSize, FW_SEMIBOLD);
    const ScopedGdiObject selectedFont(dc, titleFont.handle);

    DockPanelFramePainter{}.Paint(dc, rect, panel.kind, theme);

    RECT tabStrip{ rect.left + 1, rect.top + 1, rect.right - 1, rect.top + metrics.tabStripHeight };
    EditorSurfacePainter::Fill(dc, tabStrip, theme, EditorSurfaceKind::HeaderStrip);

    RECT tab{ tabStrip.left, tabStrip.top, std::min(tabStrip.left + metrics.tabWidth, tabStrip.right), tabStrip.bottom };
    DrawTabChrome(dc, tab, active, theme);
    DrawTabContent(dc, tab, panel, theme, active);
}

} // namespace kb::editor

#endif
