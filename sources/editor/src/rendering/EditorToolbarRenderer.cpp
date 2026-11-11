#include "rendering/EditorToolbarRenderer.hpp"

#if defined(_WIN32)
#include "project/EditorProjectPaths.hpp"
#include "rendering/EditorPanelStyle.hpp"
#include "rendering/EditorSurfacePainter.hpp"
#include "rendering/EditorToolbarLayout.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconGdiplusRuntime.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/components/EditorDialogStyle.hpp"
#include "rendering/gdi/EditorFontCache.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "localization/EditorLocalization.hpp"
#include "scene/EditorSceneContext.hpp"
#include "platform/win32/Utf8Gdi.hpp"

#pragma warning(push, 0)
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#pragma warning(pop)

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace kb::editor {
namespace {

constexpr int kTransportIconInset = 6;
constexpr int kToolbarButtonRadius = 6;
constexpr float kBarControlRadius = 3.0F;
constexpr int kBarIconSize = 16;
constexpr float kTabRadius = 4.0F;
constexpr float kDropdownRowRadius = 3.0F;
constexpr int kDropdownIconColumn = 24;
constexpr int kDropdownIconSize = 18;
constexpr int kDropdownRowPadding = 10;
constexpr COLORREF kDropdownDangerText = RGB(255, 138, 128);
constexpr COLORREF kBarHoverFill = RGB(87, 87, 87);
constexpr COLORREF kCloseHoverFill = RGB(196, 43, 28);

[[nodiscard]] COLORREF Blend(COLORREF a, COLORREF b, int numerator, int denominator) noexcept {
    const int inv = denominator - numerator;
    return RGB(
        (GetRValue(a) * inv + GetRValue(b) * numerator) / denominator,
        (GetGValue(a) * inv + GetGValue(b) * numerator) / denominator,
        (GetBValue(a) * inv + GetBValue(b) * numerator) / denominator);
}

[[nodiscard]] COLORREF BlendRatio(COLORREF a, COLORREF b, double ratio) noexcept {
    const double t = std::clamp(ratio, 0.0, 1.0);
    const double inv = 1.0 - t;
    return RGB(
        static_cast<int>((static_cast<double>(GetRValue(a)) * inv) + (static_cast<double>(GetRValue(b)) * t)),
        static_cast<int>((static_cast<double>(GetGValue(a)) * inv) + (static_cast<double>(GetGValue(b)) * t)),
        static_cast<int>((static_cast<double>(GetBValue(a)) * inv) + (static_cast<double>(GetBValue(b)) * t)));
}

[[nodiscard]] COLORREF ThemeColor(const EditorColor& color) noexcept {
    return RGB(color.r, color.g, color.b);
}

[[nodiscard]] double ToolbarPulse() noexcept {
    const double seconds = static_cast<double>(GetTickCount64() % 100000ULL) / 1000.0;
    return (std::sin(seconds * 3.25) + 1.0) * 0.5;
}

void DrawMenuText(HDC dc, RECT rect, std::string_view label, COLORREF text, UINT format) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, text);
    kb::editor::DrawTextUtf8(dc, label.data(), static_cast<int>(label.size()), &rect, format | DT_NOPREFIX);
}

void DrawStatusText(HDC dc, RECT rect, std::string_view label, COLORREF text, UINT format) {
    if (rect.right <= rect.left || rect.bottom <= rect.top || label.empty()) {
        return;
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, text);
    kb::editor::DrawTextUtf8(dc, label.data(), static_cast<int>(label.size()), &rect, format | DT_NOPREFIX | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
}

[[nodiscard]] std::string SceneDisplayName(const EditorSceneContext& sceneContext) {
    const std::filesystem::path& path = sceneContext.CurrentScenePath();
    const std::string filename = path.filename().string();
    if (!filename.empty()) {
        return filename;
    }
    return std::string{ EditorText("toolbar.untitledScene", "Untitled.21kbscene") };
}

ULONGLONG g_nextBranchRead = 0;

// The project's checked-out branch. Read from disk at most every few seconds, because the
// bar repaints on every pointer move.
[[nodiscard]] const std::string& CurrentBranch() {
    static std::string cached;
    const ULONGLONG now = GetTickCount64();
    if (now < g_nextBranchRead) {
        return cached;
    }
    g_nextBranchRead = now + 5000ULL;
    cached.clear();
    std::ifstream head(EditorProjectPaths::ProjectRoot() / ".git" / "HEAD");
    std::string line;
    constexpr std::string_view prefix = "ref: refs/heads/";
    if (head && std::getline(head, line) && line.starts_with(prefix)) {
        cached = line.substr(prefix.size());
    }
    return cached;
}

void FillRound(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int radius) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

[[nodiscard]] COLORREF FlatButtonFill(const EditorTheme& theme, bool enabled, bool hovered, bool pressed) noexcept {
    if (!enabled) {
        return ThemeColor(theme.panel);
    }
    if (pressed) {
        return ThemeColor(theme.chrome);
    }
    return hovered ? RGB(87, 87, 87) : ThemeColor(theme.toolbarButton);
}

} // namespace

EditorMenuRects EditorToolbarRenderer::ResolveMenu(
    const RECT& rect, EditorMenuCommand openMenu, int rowCount) noexcept {
    return EditorToolbarLayout::ResolveMenu(rect, openMenu, rowCount);
}

EditorToolbarRects EditorToolbarRenderer::ResolveToolbar(const RECT& rect) noexcept {
    return EditorToolbarLayout::ResolveToolbar(rect);
}

EditorStatusBarRects EditorToolbarRenderer::ResolveStatusBar(const RECT& rect) noexcept {
    return EditorToolbarLayout::ResolveStatusBar(rect);
}

bool EditorToolbarRenderer::HitTestStatusBarConsole(const EditorStatusBarRects& rects, int x, int y) noexcept {
    const auto inside = [x, y](const RECT& rect) noexcept {
        return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
    };
    return inside(rects.errors) || inside(rects.warnings);
}

bool EditorToolbarRenderer::HitTestStatusBarAgent(const EditorStatusBarRects& rects, int x, int y) noexcept {
    return x >= rects.askAgent.left && x < rects.askAgent.right && y >= rects.askAgent.top && y < rects.askAgent.bottom;
}

EditorMenuCommand EditorToolbarRenderer::HitTestMenu(const EditorMenuRects& rects, int x, int y) noexcept {
    return EditorToolbarLayout::HitTestMenu(rects, x, y);
}

std::optional<int> EditorToolbarRenderer::HitTestMenuRow(const EditorMenuRects& rects, int x, int y) noexcept {
    return EditorToolbarLayout::HitTestMenuRow(rects, x, y);
}

EditorTransportCommand EditorToolbarRenderer::HitTestTransport(const EditorToolbarRects& rects, int x, int y) noexcept {
    return EditorToolbarLayout::HitTestTransport(rects, x, y);
}

bool EditorToolbarRenderer::HitTestSave(const EditorToolbarRects& rects, int x, int y) noexcept {
    return EditorToolbarLayout::HitTestSave(rects, x, y);
}

EditorTopBarItem EditorToolbarRenderer::HitTestTopBarItem(const EditorToolbarRects& rects, int x, int y) noexcept {
    return EditorToolbarLayout::HitTestTopBarItem(rects, x, y);
}

RECT EditorToolbarRenderer::MenuDropdownPaintBounds(const EditorMenuRects& menu) noexcept {
    if (menu.dropdown.right <= menu.dropdown.left || menu.dropdown.bottom <= menu.dropdown.top) {
        return RECT{};
    }
    return menu.dropdown;
}

void EditorToolbarRenderer::InvalidateRepositoryState() noexcept {
    g_nextBranchRead = 0;
}

namespace {

[[nodiscard]] HeroIconKind LayoutRowIcon(const EditorLayoutMenuRow& row) noexcept {
    if (row.active) {
        return HeroIconKind::Check;
    }
    switch (row.action) {
    case EditorLayoutMenuAction::Save:
        return HeroIconKind::Plus;
    case EditorLayoutMenuAction::Delete:
    case EditorLayoutMenuAction::Remove:
        return HeroIconKind::XMark;
    case EditorLayoutMenuAction::Default:
    case EditorLayoutMenuAction::Apply:
    default:
        return HeroIconKind::RectangleGroup;
    }
}

} // namespace

void EditorToolbarRenderer::PaintMenu(HDC dc, const RECT& rect, const EditorTheme& theme, const EditorShellInteractionState& interaction) const {
    const EditorMenuRects menu =
        ResolveMenu(rect, interaction.OpenMenu(), interaction.MenuRowCount(interaction.OpenMenu()));
    // Only the titles' part of the row: the toolbar painted the rest of it.
    EditorSurfacePainter::Fill(dc, RECT{ menu.file.left - 2, rect.top, menu.help.right + 4, rect.bottom }, theme, EditorSurfaceKind::HeaderStrip);
    const COLORREF textPrimary = ThemeColor(theme.textPrimary);
    const COLORREF textSecondary = ThemeColor(theme.textSecondary);
    const COLORREF hoverFill = ThemeColor(theme.toolbarButton);
    const COLORREF openFill = ThemeColor(theme.header);
    // The font the layout measured the titles with.
    const ScopedFont menuFont(EditorToolbarLayout::MenuFontSize, FW_NORMAL);
    const ScopedGdiObject selectedFont(dc, menuFont.handle);

    for (const EditorMenuDescriptor& descriptor : EditorToolbarLayout::MenuDescriptors()) {
        const RECT item = EditorToolbarLayout::MenuRectByCommand(menu, descriptor.command);
        const bool open = interaction.OpenMenu() == descriptor.command;
        const bool hovered = interaction.HoveredMenu() == descriptor.command;
        if (open) {
            panel_style::FillDockTab(dc, item, openFill, kTabRadius);
        } else if (hovered) {
            panel_style::DrawRoundedFrame(dc, item, hoverFill, hoverFill, kBarControlRadius, 1.0F);
        }
        DrawMenuText(dc, item, EditorToolbarLayout::MenuLabel(descriptor.command), open || hovered ? textPrimary : textSecondary, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

namespace {

struct DropdownRowView {
    std::string_view label;
    std::string_view detail;
    std::string_view shortcut;
    HeroIconKind icon = HeroIconKind::RectangleGroup;
    bool enabled = true;
    bool selected = false;
    bool danger = false;
    bool submenu = false;
};

[[nodiscard]] DropdownRowView ResolveDropdownRow(EditorMenuCommand openMenu, int row, const EditorShellInteractionState& interaction) {
    if (openMenu == EditorMenuCommand::Layout) {
        const EditorLayoutMenuRow* layoutRow = interaction.LayoutMenu().Row(row);
        if (layoutRow == nullptr) {
            return {};
        }
        return { .label = layoutRow->label, .icon = LayoutRowIcon(*layoutRow), .enabled = layoutRow->enabled };
    }
    if (openMenu == EditorMenuCommand::RecentProjects) {
        if (row == 0) {
            return { .label = EditorText("common.back", "Back"), .icon = HeroIconKind::ChevronLeft };
        }
        const auto& projects = interaction.RecentProjectRows();
        const auto index = static_cast<std::size_t>(row - 1);
        if (index >= projects.size()) {
            return {};
        }
        return { .label = projects[index].label, .detail = projects[index].detail, .icon = HeroIconKind::Cube, .enabled = !projects[index].path.empty() };
    }
    int fixedRow = row;
    if (openMenu == EditorMenuCommand::Publish) {
        const auto& platforms = interaction.PublishRows();
        if (static_cast<std::size_t>(row) < platforms.size()) {
            const EditorPathMenuRow& platform = platforms[static_cast<std::size_t>(row)];
            return { .label = platform.label, .detail = platform.detail, .icon = platform.active ? HeroIconKind::RocketLaunch : HeroIconKind::Warning };
        }
        fixedRow = row - static_cast<int>(platforms.size());
    }
    if (openMenu == EditorMenuCommand::Scene) {
        const auto& scenes = interaction.SceneMenuRows();
        if (static_cast<std::size_t>(row) < scenes.size()) {
            return { .label = scenes[static_cast<std::size_t>(row)].label, .icon = HeroIconKind::Cube, .selected = scenes[static_cast<std::size_t>(row)].active };
        }
        fixedRow = row - static_cast<int>(scenes.size());
    }
    DropdownRowView view{
        .label = EditorToolbarLayout::DropdownLabel(openMenu, fixedRow),
        .detail = EditorToolbarLayout::DropdownDescription(openMenu, fixedRow),
        .shortcut = EditorToolbarLayout::DropdownShortcut(openMenu, fixedRow),
        .icon = EditorToolbarLayout::DropdownIcon(openMenu, fixedRow),
    };
    if (openMenu == EditorMenuCommand::PlayOptions) {
        const EditorPlayPreferences& play = interaction.PlayMenuPreferences();
        view.enabled = interaction.PlayMenuCanChange();
        if (fixedRow < 3) {
            view.selected = static_cast<int>(play.target) == fixedRow;
        } else if (play.startPaused) {
            view.icon = HeroIconKind::Check;
        }
    } else if (openMenu == EditorMenuCommand::Project) {
        view.submenu = fixedRow == 2;
    } else if (openMenu == EditorMenuCommand::Scene) {
        view.enabled = interaction.SceneDocumentActionsEnabled() || fixedRow < 2 || fixedRow == 4;
        view.danger = fixedRow == 5;
    }
    return view;
}

void PaintDropdownRow(HDC dc, const RECT& rect, const EditorTheme& theme, const DropdownRowView& row, bool hovered) {
    if (row.selected) {
        const COLORREF accent = ThemeColor(theme.accent);
        panel_style::DrawRoundedFrame(dc, rect, accent, accent, kDropdownRowRadius, 1.0F);
    } else if (hovered && row.enabled) {
        const COLORREF hover = ThemeColor(theme.toolbarButton);
        panel_style::DrawRoundedFrame(dc, rect, hover, hover, kDropdownRowRadius, 1.0F);
    }
    const COLORREF white = RGB(255, 255, 255);
    const COLORREF disabled = ThemeColor(theme.textDisabled);
    const COLORREF labelColor = !row.enabled ? disabled : (row.selected ? white : (row.danger ? kDropdownDangerText : ThemeColor(theme.textPrimary)));
    const COLORREF mutedColor = !row.enabled ? disabled : (row.selected ? white : ThemeColor(theme.textSecondary));

    const int columnLeft = static_cast<int>(rect.left) + kDropdownRowPadding;
    const int iconLeft = columnLeft + (kDropdownIconColumn - kDropdownIconSize) / 2;
    const int centerY = static_cast<int>(rect.top + rect.bottom) / 2;
    HeroIconPainter::Draw(dc, RECT{ iconLeft, centerY - kDropdownIconSize / 2, iconLeft + kDropdownIconSize, centerY + kDropdownIconSize / 2 }, row.icon, mutedColor, 1);

    int textRight = static_cast<int>(rect.right) - kDropdownRowPadding;
    const ScopedFont labelFont(EditorToolbarLayout::MenuFontSize, FW_NORMAL);
    const ScopedFont detailFont(EditorToolbarLayout::DropdownDetailFontSize, FW_NORMAL);
    const ScopedGdiObject selectedFont(dc, labelFont.handle);
    if (!row.shortcut.empty()) {
        const int width = EditorDialogStyle::TextWidth(dc, row.shortcut, EditorToolbarLayout::MenuFontSize);
        DrawStatusText(dc, RECT{ textRight - width, rect.top, textRight, rect.bottom }, row.shortcut, mutedColor, DT_RIGHT);
        textRight -= width + 12;
    }
    if (row.submenu) {
        constexpr int chevron = 12;
        HeroIconPainter::Draw(dc, RECT{ textRight - chevron, centerY - chevron / 2, textRight, centerY + chevron / 2 }, HeroIconKind::ChevronRight, mutedColor, 1);
        textRight -= chevron + 8;
    }
    const int textLeft = columnLeft + kDropdownIconColumn + kDropdownRowPadding;
    if (row.detail.empty()) {
        DrawStatusText(dc, RECT{ textLeft, rect.top, textRight, rect.bottom }, row.label, labelColor, DT_LEFT);
        return;
    }
    DrawStatusText(dc, RECT{ textLeft, rect.top + 5, textRight, centerY + 2 }, row.label, labelColor, DT_LEFT);
    const ScopedGdiObject smallFont(dc, detailFont.handle);
    DrawStatusText(dc, RECT{ textLeft, centerY + 1, textRight, rect.bottom - 5 }, row.detail, mutedColor, DT_LEFT);
}

} // namespace

void EditorToolbarRenderer::PaintMenuDropdown(HDC dc, const EditorMenuRects& menu, const EditorTheme& theme, const EditorShellInteractionState& interaction) const {
    const EditorMenuCommand openMenu = interaction.OpenMenu();
    if (EditorToolbarLayout::MenuIndex(openMenu) < 0) {
        return;
    }
    GdiDrawing::FillRectColor(dc, menu.dropdown, ThemeColor(theme.header));

    const ScopedFont headingFont(12, FW_SEMIBOLD);
    for (int row = 0; row < menu.dropdownRowCount; ++row) {
        const RECT rowRect = menu.dropdownRows[static_cast<std::size_t>(row)];
        int above = static_cast<int>(rowRect.top);
        if (EditorToolbarLayout::DropdownHasHeading(openMenu, row, menu.dropdownRowCount)) {
            above -= EditorToolbarLayout::DropdownHeadingHeight;
            const ScopedGdiObject selectedFont(dc, headingFont.handle);
            DrawStatusText(dc, RECT{ rowRect.left + kDropdownRowPadding, above + 8, rowRect.right - kDropdownRowPadding, static_cast<int>(rowRect.top) - 4 },
                EditorToolbarLayout::DropdownHeading(openMenu, row, menu.dropdownRowCount), RGB(168, 168, 168), DT_LEFT);
        }
        if (EditorToolbarLayout::DropdownStartsGroup(openMenu, row, menu.dropdownRowCount)) {
            const int ruleY = above - EditorToolbarLayout::DropdownSeparatorHeight / 2 - 1;
            GdiDrawing::FillRectColor(dc, RECT{ rowRect.left + 4, ruleY, rowRect.right - 4, ruleY + 1 }, ThemeColor(theme.toolbarButton));
        }
        const bool hovered = interaction.HoveredMenuRow().has_value() && *interaction.HoveredMenuRow() == row;
        PaintDropdownRow(dc, rowRect, theme, ResolveDropdownRow(openMenu, row, interaction), hovered);
    }
}

void EditorToolbarRenderer::PaintToolbar(HDC dc, const RECT& rect, const EditorTheme& theme, const EditorSceneContext& sceneContext, const EditorPlayModeState& playMode, const EditorShellInteractionState& interaction) const {
    EditorSurfacePainter::Fill(dc, rect, theme, EditorSurfaceKind::HeaderStrip);
    const EditorToolbarRects toolbar = ResolveToolbar(rect);
    const ScopedFont normalFont(EditorToolbarLayout::MenuFontSize, FW_NORMAL);
    const ScopedFont boldFont(EditorToolbarLayout::MenuFontSize, FW_BOLD);
    const ScopedGdiObject selectedFont(dc, normalFont.handle);

    const bool isPlaying = playMode.Mode() == EditorPlayMode::Playing;
    const bool isPaused = playMode.Mode() == EditorPlayMode::Paused;
    const bool isStopped = playMode.Mode() == EditorPlayMode::Stopped;
    const bool transportActive = isPlaying || isPaused;
    const EditorTransportCommand hovered = interaction.HoveredTransport();
    const EditorTransportCommand pressed = interaction.PressedTransport();
    const EditorTopBarItem hoveredItem = interaction.HoveredTopBarItem();
    const COLORREF success = RGB(60, 203, 108);
    const COLORREF warning = RGB(224, 165, 54);
    const COLORREF danger = RGB(226, 74, 74);
    const COLORREF ink = ThemeColor(theme.textPrimary);
    const COLORREF muted = ThemeColor(theme.textSecondary);
    const COLORREF disabled = ThemeColor(theme.textDisabled);
    const COLORREF panel = ThemeColor(theme.panel);
    const COLORREF button = ThemeColor(theme.toolbarButton);
    const COLORREF white = RGB(255, 255, 255);

    const auto fillControl = [dc](const RECT& area, COLORREF fill) {
        panel_style::DrawRoundedFrame(dc, area, fill, fill, kBarControlRadius, 1.0F);
    };
    const auto iconAt = [dc](int left, const RECT& area, HeroIconKind icon, COLORREF color, int size = kBarIconSize) {
        const int top = static_cast<int>(area.top) + (static_cast<int>(area.bottom - area.top) - size) / 2;
        HeroIconPainter::Draw(dc, RECT{ left, top, left + size, top + size }, icon, color, 1);
    };
    const auto label = [&](const RECT& area, int left, int rightInset, std::string_view text, COLORREF color, bool bold) {
        const ScopedGdiObject font(dc, bold ? boldFont.handle : normalFont.handle);
        DrawStatusText(dc, RECT{ left, area.top, static_cast<int>(area.right) - rightInset, area.bottom }, text, color, DT_LEFT);
    };

    // Project, scene and branch.
    const EditorMenuCommand openMenu = interaction.OpenMenu();
    const COLORREF tab = ThemeColor(theme.header);
    if (openMenu == EditorMenuCommand::Project || openMenu == EditorMenuCommand::RecentProjects) {
        panel_style::FillDockTab(dc, toolbar.projectButton, tab, kTabRadius);
    } else {
        fillControl(toolbar.projectButton, hoveredItem == EditorTopBarItem::Project ? button : panel);
    }
    iconAt(toolbar.projectButton.left + 10, toolbar.projectButton, HeroIconKind::Cube, ink);
    label(toolbar.projectButton, toolbar.projectButton.left + 32, 24, EditorProjectPaths::ProjectRoot().filename().string(), ink, true);
    iconAt(toolbar.projectButton.right - 20, toolbar.projectButton, HeroIconKind::ChevronDown, muted, 12);

    if (openMenu == EditorMenuCommand::Scene) {
        panel_style::FillDockTab(dc, toolbar.sceneButton, tab, kTabRadius);
    } else if (hoveredItem == EditorTopBarItem::Scene) {
        fillControl(toolbar.sceneButton, button);
    }
    const bool unsaved = sceneContext.SceneDocumentDirty() || sceneContext.HasDirtyMaterialAssetEdit();
    label(toolbar.sceneButton, toolbar.sceneButton.left + 10, unsaved ? 36 : 24, SceneDisplayName(sceneContext), ink, false);
    if (unsaved) {
        const int centerY = static_cast<int>(toolbar.sceneButton.top + toolbar.sceneButton.bottom) / 2;
        const RECT dot{ toolbar.sceneButton.right - 32, centerY - 3, toolbar.sceneButton.right - 26, centerY + 3 };
        panel_style::DrawRoundedFrame(dc, dot, warning, warning, 3.0F, 1.0F);
    }
    iconAt(toolbar.sceneButton.right - 20, toolbar.sceneButton, HeroIconKind::ChevronDown, muted, 12);

    if (interaction.RepositoryWindowOpen()) {
        panel_style::FillDockTab(dc, toolbar.branchLabel, tab, kTabRadius);
    } else if (hoveredItem == EditorTopBarItem::Branch) {
        fillControl(toolbar.branchLabel, button);
    }
    const std::string& branch = CurrentBranch();
    label(toolbar.branchLabel, toolbar.branchLabel.left + 6, 0,
        branch.empty() ? EditorText("toolbar.noBranch", "No repo") : std::string_view{ branch }, interaction.RepositoryWindowOpen() ? ink : muted, false);
    const int separatorX = static_cast<int>(toolbar.branchLabel.right) + 4;
    const int centerRow = static_cast<int>(rect.top + rect.bottom) / 2;
    GdiDrawing::FillRectColor(dc, RECT{ separatorX, centerRow - 11, separatorX + 1, centerRow + 11 }, button);

    // Transport.
    const bool playHovered = isStopped && hovered == EditorTransportCommand::Play;
    const bool playPressed = isStopped && pressed == EditorTransportCommand::Play;
    const bool pauseHovered = transportActive && hovered == EditorTransportCommand::Pause;
    const bool pausePressed = transportActive && pressed == EditorTransportCommand::Pause;
    const bool stopHovered = transportActive && hovered == EditorTransportCommand::Stop;
    const bool stopPressed = transportActive && pressed == EditorTransportCommand::Stop;
    const COLORREF playFill = !isStopped ? panel : (playPressed ? ThemeColor(theme.chrome) : (playHovered ? kBarHoverFill : button));
    fillControl(toolbar.playButton, playFill);
    iconAt(toolbar.playButton.left + 16, toolbar.playButton, HeroIconKind::Play, isStopped ? success : disabled);
    label(toolbar.playButton, toolbar.playButton.left + 40, 6, EditorText("toolbar.play", "Play"), isStopped ? ink : disabled, true);
    if (openMenu == EditorMenuCommand::PlayOptions) {
        panel_style::FillDockTab(dc, toolbar.playOptionsButton, tab, kTabRadius);
    } else {
        fillControl(toolbar.playOptionsButton, hoveredItem == EditorTopBarItem::PlayOptions ? kBarHoverFill : playFill);
    }
    iconAt(toolbar.playOptionsButton.left + 6, toolbar.playOptionsButton, HeroIconKind::ChevronDown, muted, 12);

    PaintButton(dc, toolbar.pauseButton, theme, transportActive, isPaused, pauseHovered, pausePressed, isPaused ? success : RGB(0, 0, 0));
    PaintButton(dc, toolbar.stopButton, theme, transportActive, false, stopHovered, stopPressed, RGB(0, 0, 0));
    iconAt(toolbar.pauseButton.left + 11, toolbar.pauseButton, isPaused ? HeroIconKind::Resume : HeroIconKind::Pause,
        transportActive ? (isPaused ? success : warning) : disabled);
    iconAt(toolbar.stopButton.left + 11, toolbar.stopButton, HeroIconKind::TransportStop, transportActive ? danger : disabled);

    // Build and Publish.
    if (toolbar.buildButton.right > toolbar.buildButton.left) {
        fillControl(toolbar.buildButton, hoveredItem == EditorTopBarItem::Build ? button : panel);
        iconAt(toolbar.buildButton.left + 12, toolbar.buildButton, HeroIconKind::WrenchScrewdriver, ink);
        label(toolbar.buildButton, toolbar.buildButton.left + 36, 6, EditorText("toolbar.build", "Build"), ink, false);
        const COLORREF accent = ThemeColor(theme.accent);
        if (openMenu == EditorMenuCommand::Publish) {
            panel_style::FillDockTab(dc, toolbar.publishButton, tab, kTabRadius);
        } else {
            fillControl(toolbar.publishButton, hoveredItem == EditorTopBarItem::Publish ? BlendRatio(accent, white, 0.16) : accent);
        }
        iconAt(toolbar.publishButton.left + 12, toolbar.publishButton, HeroIconKind::RocketLaunch, white);
        label(toolbar.publishButton, toolbar.publishButton.left + 36, 6, EditorText("toolbar.publish", "Publish"), white, true);
    }

    // The window's own controls, now that the bar takes the title row's place.
    if (hoveredItem == EditorTopBarItem::Minimize) {
        GdiDrawing::FillRectColor(dc, toolbar.minimizeButton, button);
    }
    iconAt(toolbar.minimizeButton.left + 16, toolbar.minimizeButton, HeroIconKind::Minus, ink, 14);
    if (hoveredItem == EditorTopBarItem::Maximize) {
        GdiDrawing::FillRectColor(dc, toolbar.maximizeButton, button);
    }
    const int maxLeft = static_cast<int>(toolbar.maximizeButton.left) + 18;
    const int maxTop = centerRow - 5;
    const RECT square{ maxLeft, maxTop, maxLeft + 10, maxTop + 10 };
    GdiDrawing::FillRectColor(dc, RECT{ square.left, square.top, square.right, square.top + 1 }, ink);
    GdiDrawing::FillRectColor(dc, RECT{ square.left, square.bottom - 1, square.right, square.bottom }, ink);
    GdiDrawing::FillRectColor(dc, RECT{ square.left, square.top, square.left + 1, square.bottom }, ink);
    GdiDrawing::FillRectColor(dc, RECT{ square.right - 1, square.top, square.right, square.bottom }, ink);
    const bool closeOver = hoveredItem == EditorTopBarItem::Close;
    if (closeOver) {
        GdiDrawing::FillRectColor(dc, toolbar.closeButton, kCloseHoverFill);
    }
    iconAt(toolbar.closeButton.left + 16, toolbar.closeButton, HeroIconKind::XMark, closeOver ? white : ink, 14);
}

void EditorToolbarRenderer::PaintStatusBar(
    HDC dc,
    const RECT& rect,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext,
    const EditorPlayModeState& playMode,
    std::string_view backendLabel,
    bool drawerCollapsed) const {
    if (rect.right <= rect.left || rect.bottom <= rect.top) {
        return;
    }
    GdiDrawing::FillRectColor(dc, rect, ThemeColor(theme.chrome));
    GdiDrawing::FillRectColor(dc, RECT{ rect.left, rect.top, rect.right, rect.top + 1 }, ThemeColor(theme.borderChrome));
    const EditorStatusBarRects bar = ResolveStatusBar(rect);
    const ScopedFont font(EditorFontCache::UiPointSize, FW_NORMAL);
    const ScopedGdiObject selectedFont(dc, font.handle);
    const COLORREF success = RGB(63, 194, 123);
    const COLORREF warning = RGB(224, 165, 54);
    const COLORREF danger = RGB(232, 88, 88);
    const COLORREF secondary = ThemeColor(theme.textSecondary);
    const COLORREF dim = ThemeColor(theme.textDisabled);
    const auto item = [dc](const RECT& area, HeroIconKind icon, COLORREF iconColor, std::string_view text, COLORREF textColor) {
        constexpr int iconSize = 14;
        const int iconTop = static_cast<int>(area.top) + (static_cast<int>(area.bottom - area.top) - iconSize) / 2;
        HeroIconPainter::Draw(dc, RECT{ area.left, iconTop, area.left + iconSize, iconTop + iconSize }, icon, iconColor, 1);
        DrawStatusText(dc, RECT{ area.left + iconSize + 6, area.top, area.right, area.bottom }, text, textColor, DT_LEFT);
    };

    switch (playMode.Mode()) {
    case EditorPlayMode::Playing:
        item(bar.state, HeroIconKind::Play, success, EditorText("toolbar.playing", "Playing"), success);
        break;
    case EditorPlayMode::Paused:
        item(bar.state, HeroIconKind::Pause, warning, EditorText("toolbar.paused", "Paused"), warning);
        break;
    case EditorPlayMode::Stopped:
        item(bar.state, HeroIconKind::CheckCircle, success, EditorText("statusBar.ready", "Ready"), secondary);
        break;
    }

    const std::uint32_t errors = sceneContext.Console().Count(EditorConsoleLevel::Error);
    const std::uint32_t warnings = sceneContext.Console().Count(EditorConsoleLevel::Warning);
    // A count of zero stays quiet; anything else takes the level's colour so it is noticed
    // without the Console being open.
    item(bar.errors, HeroIconKind::XCircle, errors > 0U ? danger : dim,
        EditorLocalization::Format("statusBar.errors", "{count} errors", { { "count", std::to_string(errors) } }),
        errors > 0U ? danger : secondary);
    item(bar.warnings, HeroIconKind::Warning, warnings > 0U ? warning : dim,
        EditorLocalization::Format("statusBar.warnings", "{count} warnings", { { "count", std::to_string(warnings) } }),
        warnings > 0U ? warning : secondary);
    item(bar.askAgent, HeroIconKind::Bolt, ThemeColor(theme.accent), EditorText("statusBar.askAgent", "Ask Agent"), ThemeColor(theme.textPrimary));
    if (drawerCollapsed) {
        DrawStatusText(dc, bar.backend, EditorText("statusBar.drawerHint", "Ctrl+Space  Project Files / Console"), secondary, DT_LEFT);
    }
    DrawStatusText(dc, bar.backend, backendLabel, dim, DT_RIGHT);
}

void EditorToolbarRenderer::PaintButton(HDC dc, const RECT& rect, const EditorTheme& theme, bool enabled, bool active, bool hovered, bool pressed, COLORREF glow) const {
    const COLORREF fill = FlatButtonFill(theme, enabled, hovered, pressed);
    const COLORREF border = active && glow != RGB(0, 0, 0) ? glow : fill;
    FillRound(dc, rect, fill, border, kToolbarButtonRadius);
}

} // namespace kb::editor

#endif
