#include "rendering/EditorToolbarRenderer.hpp"

#if defined(_WIN32)
#include "rendering/EditorSurfacePainter.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconGdiplusRuntime.hpp"
#include "rendering/HeroIconPainter.hpp"

#pragma warning(push, 0)
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#pragma warning(pop)

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>

namespace kb::editor {
namespace {

constexpr int kTransportButtonSize = 36;
constexpr int kTransportButtonGap = 6;
constexpr int kTransportIconInset = 7;
constexpr int kTransportVisualOffsetY = 4;
constexpr int kToolbarButtonRadius = 8;
constexpr int kMenuLeftInset = 10;
constexpr int kMenuTopInset = 2;
constexpr int kMenuItemHeightPad = 3;
constexpr int kDropdownTopGap = 4;
constexpr int kDropdownWidth = 230;
constexpr int kDropdownRowHeight = 30;
constexpr int kDropdownRadius = 7;

struct MenuDescriptor {
    EditorMenuCommand command;
    std::string_view label;
    int width;
};

constexpr std::array<MenuDescriptor, 4> kMenus{{
    { EditorMenuCommand::File, "File", 54 },
    { EditorMenuCommand::Edit, "Edit", 54 },
    { EditorMenuCommand::Options, "Options", 82 },
    { EditorMenuCommand::Help, "Help", 58 },
}};

constexpr std::array<std::array<std::string_view, 4>, 4> kDropdownRows{{
    { "New Scene", "Open...", "Save", "Exit" },
    { "Undo", "Redo", "Duplicate", "Preferences" },
    { "Renderer", "Layout", "Project Settings", "Editor Settings" },
    { "Documentation", "Report Issue", "Release Notes", "About" },
}};

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

[[nodiscard]] RECT OffsetRectCopy(RECT rect, int dx, int dy) noexcept {
    OffsetRect(&rect, dx, dy);
    return rect;
}

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] RECT ButtonRect(const RECT& toolbar, int left, int size) noexcept {
    const int toolbarHeight = static_cast<int>(toolbar.bottom - toolbar.top);
    const int top = toolbar.top + std::max(0, (toolbarHeight - size) / 2) + kTransportVisualOffsetY;
    return RECT{
        .left = toolbar.left + left,
        .top = top,
        .right = toolbar.left + left + size,
        .bottom = top + size,
    };
}

[[nodiscard]] int MenuIndex(EditorMenuCommand menu) noexcept {
    switch (menu) {
    case EditorMenuCommand::File:
        return 0;
    case EditorMenuCommand::Edit:
        return 1;
    case EditorMenuCommand::Options:
        return 2;
    case EditorMenuCommand::Help:
        return 3;
    case EditorMenuCommand::None:
    default:
        return -1;
    }
}

[[nodiscard]] RECT MenuRectByCommand(const EditorMenuRects& rects, EditorMenuCommand menu) noexcept {
    switch (menu) {
    case EditorMenuCommand::File:
        return rects.file;
    case EditorMenuCommand::Edit:
        return rects.edit;
    case EditorMenuCommand::Options:
        return rects.options;
    case EditorMenuCommand::Help:
        return rects.help;
    case EditorMenuCommand::None:
    default:
        return RECT{};
    }
}

void DrawMenuText(HDC dc, const RECT& rect, std::string_view label, COLORREF text) {
    SetBkMode(dc, TRANSPARENT);
    const std::string textValue(label);
    GdiDrawing::DrawTextBlock(dc, rect, textValue.c_str(), text);
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

void StrokeRound(HDC dc, const RECT& rect, COLORREF color, int radius, int width) {
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void AddRoundedRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float radius) {
    const float diameter = radius * 2.0F;
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0F, 90.0F);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter, 270.0F, 90.0F);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter, diameter, diameter, 0.0F, 90.0F);
    path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter, 90.0F, 90.0F);
    path.CloseFigure();
}

void FillRoundedAlpha(HDC dc, const RECT& rect, COLORREF color, BYTE alpha, float radius) {
    if (alpha == 0U) {
        return;
    }
    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath path;
    AddRoundedRect(
        path,
        Gdiplus::RectF(
            static_cast<float>(rect.left),
            static_cast<float>(rect.top),
            static_cast<float>(rect.right - rect.left),
            static_cast<float>(rect.bottom - rect.top)),
        radius);
    Gdiplus::SolidBrush brush(Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color)));
    graphics.FillPath(&brush, &path);
}

} // namespace

EditorMenuRects EditorToolbarRenderer::ResolveMenu(const RECT& rect, EditorMenuCommand openMenu) noexcept {
    EditorMenuRects menu{};
    menu.menuBar = rect;
    int left = rect.left + kMenuLeftInset;
    for (const MenuDescriptor& descriptor : kMenus) {
        RECT item{
            .left = left,
            .top = rect.top + kMenuTopInset,
            .right = left + descriptor.width,
            .bottom = rect.bottom - kMenuItemHeightPad,
        };
        switch (descriptor.command) {
        case EditorMenuCommand::File:
            menu.file = item;
            break;
        case EditorMenuCommand::Edit:
            menu.edit = item;
            break;
        case EditorMenuCommand::Options:
            menu.options = item;
            break;
        case EditorMenuCommand::Help:
            menu.help = item;
            break;
        case EditorMenuCommand::None:
        default:
            break;
        }
        left += descriptor.width;
    }

    const RECT anchor = MenuRectByCommand(menu, openMenu);
    if (openMenu != EditorMenuCommand::None) {
        const int dropX = std::min(std::max(rect.left + 4, anchor.left), std::max(rect.left + 4, rect.right - kDropdownWidth - 4));
        const int dropY = rect.bottom + kDropdownTopGap;
        menu.dropdown = RECT{ dropX, dropY, dropX + kDropdownWidth, dropY + (kDropdownRowHeight * 4) };
        for (int i = 0; i < 4; ++i) {
            menu.dropdownRows[static_cast<std::size_t>(i)] = RECT{
                menu.dropdown.left,
                menu.dropdown.top + (i * kDropdownRowHeight),
                menu.dropdown.right,
                menu.dropdown.top + ((i + 1) * kDropdownRowHeight),
            };
        }
    }
    return menu;
}

EditorToolbarRects EditorToolbarRenderer::ResolveToolbar(const RECT& rect) noexcept {
    EditorToolbarRects toolbar{};
    toolbar.toolbar = rect;
    const int totalWidth = kTransportButtonSize * 3 + kTransportButtonGap * 2;
    const int toolbarWidth = static_cast<int>(rect.right - rect.left);
    const int left = rect.left + std::max(0, (toolbarWidth - totalWidth) / 2);
    toolbar.playButton = ButtonRect(rect, left - rect.left, kTransportButtonSize);
    toolbar.pauseButton = ButtonRect(rect, left - rect.left + kTransportButtonSize + kTransportButtonGap, kTransportButtonSize);
    toolbar.stopButton = ButtonRect(rect, left - rect.left + (kTransportButtonSize + kTransportButtonGap) * 2, kTransportButtonSize);
    return toolbar;
}

EditorMenuCommand EditorToolbarRenderer::HitTestMenu(const EditorMenuRects& rects, int x, int y) noexcept {
    if (PointInRect(rects.file, x, y)) {
        return EditorMenuCommand::File;
    }
    if (PointInRect(rects.edit, x, y)) {
        return EditorMenuCommand::Edit;
    }
    if (PointInRect(rects.options, x, y)) {
        return EditorMenuCommand::Options;
    }
    if (PointInRect(rects.help, x, y)) {
        return EditorMenuCommand::Help;
    }
    return EditorMenuCommand::None;
}

std::optional<int> EditorToolbarRenderer::HitTestMenuRow(const EditorMenuRects& rects, int x, int y) noexcept {
    for (std::size_t i = 0; i < rects.dropdownRows.size(); ++i) {
        if (PointInRect(rects.dropdownRows[i], x, y)) {
            return static_cast<int>(i);
        }
    }
    return std::nullopt;
}

EditorTransportCommand EditorToolbarRenderer::HitTestTransport(const EditorToolbarRects& rects, int x, int y) noexcept {
    if (PointInRect(rects.playButton, x, y)) {
        return EditorTransportCommand::Play;
    }
    if (PointInRect(rects.pauseButton, x, y)) {
        return EditorTransportCommand::Pause;
    }
    if (PointInRect(rects.stopButton, x, y)) {
        return EditorTransportCommand::Stop;
    }
    return EditorTransportCommand::None;
}

void EditorToolbarRenderer::PaintMenu(HDC dc, const RECT& rect, const EditorTheme& theme, const EditorShellInteractionState& interaction) const {
    EditorSurfacePainter::Fill(dc, rect, theme, EditorSurfaceKind::HeaderStrip);
    const EditorMenuRects menu = ResolveMenu(rect, interaction.OpenMenu());
    const COLORREF textPrimary = ThemeColor(theme.textPrimary);
    const COLORREF textSecondary = ThemeColor(theme.textSecondary);
    const COLORREF accent = ThemeColor(theme.accent);
    const COLORREF hoverFill = RGB(34, 39, 48);
    const COLORREF activeFill = RGB(42, 47, 56);

    for (const MenuDescriptor& descriptor : kMenus) {
        const RECT item = MenuRectByCommand(menu, descriptor.command);
        const bool open = interaction.OpenMenu() == descriptor.command;
        const bool hovered = interaction.HoveredMenu() == descriptor.command;
        if (open || hovered) {
            FillRound(dc, item, open ? activeFill : hoverFill, open ? Blend(accent, RGB(255, 255, 255), 1, 5) : RGB(48, 56, 68), 5);
            if (open) {
                GdiDrawing::FillRectColor(dc, RECT{ item.left + 3, item.top, item.right - 3, item.top + 2 }, accent);
            }
        }
        DrawMenuText(dc, RECT{ item.left + 12, item.top, item.right, item.bottom }, descriptor.label, open || hovered ? textPrimary : textSecondary);
    }

    if (interaction.OpenMenu() == EditorMenuCommand::None) {
        return;
    }

    const int index = MenuIndex(interaction.OpenMenu());
    if (index < 0) {
        return;
    }

    const RECT shadow = OffsetRectCopy(menu.dropdown, 2, 3);
    FillRound(dc, shadow, RGB(0, 0, 0), RGB(0, 0, 0), kDropdownRadius);
    FillRound(dc, menu.dropdown, RGB(19, 22, 28), RGB(78, 91, 115), kDropdownRadius);
    GdiDrawing::FillRectColor(dc, RECT{ menu.dropdown.left + 1, menu.dropdown.top + 1, menu.dropdown.right - 1, menu.dropdown.top + 3 }, accent);

    for (int row = 0; row < 4; ++row) {
        const RECT rowRect = menu.dropdownRows[static_cast<std::size_t>(row)];
        if ((row % 2) == 1) {
            GdiDrawing::FillRectColor(dc, RECT{ rowRect.left + 1, rowRect.top, rowRect.right - 1, rowRect.bottom }, RGB(22, 25, 31));
        }
        if (interaction.HoveredMenuRow().has_value() && *interaction.HoveredMenuRow() == row) {
            FillRound(dc, RECT{ rowRect.left + 4, rowRect.top + 3, rowRect.right - 4, rowRect.bottom - 3 }, RGB(37, 44, 55), RGB(56, 68, 86), 5);
        }
        if (row > 0) {
            GdiDrawing::FillRectColor(dc, RECT{ rowRect.left + 8, rowRect.top, rowRect.right - 8, rowRect.top + 1 }, RGB(34, 39, 48));
        }
        DrawMenuText(dc, RECT{ rowRect.left + 34, rowRect.top, rowRect.right - 8, rowRect.bottom }, kDropdownRows[static_cast<std::size_t>(index)][static_cast<std::size_t>(row)], textPrimary);
    }
}

void EditorToolbarRenderer::PaintToolbar(HDC dc, const RECT& rect, const EditorTheme& theme, const EditorPlayModeState& playMode, const EditorShellInteractionState& interaction) const {
    EditorSurfacePainter::Fill(dc, rect, theme, EditorSurfaceKind::AppBackground);
    const EditorToolbarRects toolbar = ResolveToolbar(rect);

    const bool isPlaying = playMode.Mode() == EditorPlayMode::Playing;
    const bool isPaused = playMode.Mode() == EditorPlayMode::Paused;
    const bool isStopped = playMode.Mode() == EditorPlayMode::Stopped;
    const bool transportActive = isPlaying || isPaused;
    const EditorTransportCommand hovered = interaction.HoveredTransport();
    const EditorTransportCommand pressed = interaction.PressedTransport();
    const COLORREF success = RGB(63, 194, 123);
    const COLORREF warning = RGB(224, 165, 54);
    const COLORREF danger = RGB(226, 74, 74);
    const COLORREF noGlow = RGB(0, 0, 0);
    const bool playHovered = isStopped && hovered == EditorTransportCommand::Play;
    const bool playPressed = isStopped && pressed == EditorTransportCommand::Play;
    const bool pauseHovered = transportActive && hovered == EditorTransportCommand::Pause;
    const bool pausePressed = transportActive && pressed == EditorTransportCommand::Pause;
    const bool stopHovered = transportActive && hovered == EditorTransportCommand::Stop;
    const bool stopPressed = transportActive && pressed == EditorTransportCommand::Stop;

    PaintButton(dc, toolbar.playButton, theme, isStopped, false, playHovered, playPressed, noGlow);
    PaintButton(dc, toolbar.pauseButton, theme, transportActive, isPaused, pauseHovered, pausePressed, isPaused ? success : noGlow);
    PaintButton(dc, toolbar.stopButton, theme, transportActive, false, stopHovered, stopPressed, noGlow);

    const double pulse = ToolbarPulse();
    const COLORREF playIdleIcon = BlendRatio(success, RGB(178, 255, 213), 0.16);
    const COLORREF playHoverIcon = BlendRatio(success, RGB(198, 255, 226), 0.30);
    const COLORREF warningIcon = BlendRatio(warning, RGB(255, 226, 144), 0.10 + (0.18 * pulse));
    const COLORREF resumeIcon = BlendRatio(success, RGB(180, 255, 212), 0.22 + (0.24 * pulse));
    const COLORREF dangerIcon = BlendRatio(danger, RGB(255, 160, 160), 0.10 + (0.18 * pulse));
    const COLORREF pauseHoverIcon = BlendRatio(warningIcon, RGB(255, 244, 190), 0.16);
    const COLORREF stopHoverIcon = BlendRatio(dangerIcon, RGB(255, 210, 210), 0.16);
    const COLORREF disabledIcon = ThemeColor(theme.textDisabled);
    const auto iconRect = [](const RECT& button, bool pressedButton) noexcept {
        RECT rect = GdiDrawing::Inset(button, kTransportIconInset);
        if (pressedButton) {
            OffsetRect(&rect, 0, 1);
        }
        return rect;
    };

    HeroIconPainter::Draw(dc, iconRect(toolbar.playButton, playPressed), HeroIconKind::Play, isStopped ? (playHovered ? playHoverIcon : playIdleIcon) : disabledIcon, 1);
    HeroIconPainter::Draw(dc, iconRect(toolbar.pauseButton, pausePressed), isPaused ? HeroIconKind::Resume : HeroIconKind::Pause, transportActive ? (isPaused ? resumeIcon : (pauseHovered ? pauseHoverIcon : warningIcon)) : disabledIcon, 1);
    HeroIconPainter::Draw(dc, iconRect(toolbar.stopButton, stopPressed), HeroIconKind::TransportStop, transportActive ? (stopHovered ? stopHoverIcon : dangerIcon) : disabledIcon, 1);
}

void EditorToolbarRenderer::PaintButton(HDC dc, const RECT& rect, const EditorTheme& theme, bool enabled, bool active, bool hovered, bool pressed, COLORREF glow) const {
    static_cast<void>(theme);
    const double pulse = ToolbarPulse();
    const COLORREF idleTop = enabled ? RGB(29, 35, 45) : RGB(23, 27, 34);
    const COLORREF idleBottom = enabled ? RGB(19, 23, 30) : RGB(19, 22, 28);
    const COLORREF hoverTop = RGB(39, 49, 64);
    const COLORREF hoverBottom = RGB(26, 32, 42);
    const COLORREF activeTop = BlendRatio(RGB(42, 51, 64), RGB(50, 61, 78), 0.28 * pulse);
    const COLORREF activeBottom = BlendRatio(RGB(25, 31, 40), RGB(31, 38, 50), 0.22 * pulse);
    const bool hasGlow = active && glow != RGB(0, 0, 0);
    const COLORREF glowFillTop = hasGlow ? BlendRatio(activeTop, glow, 0.14 + (0.08 * pulse)) : activeTop;
    const COLORREF glowFillBottom = hasGlow ? BlendRatio(activeBottom, glow, 0.08 + (0.06 * pulse)) : activeBottom;
    const COLORREF topFill = active ? glowFillTop : (hovered ? hoverTop : idleTop);
    const COLORREF bottomFill = active ? glowFillBottom : (hovered ? hoverBottom : idleBottom);
    const COLORREF border = hasGlow ? BlendRatio(RGB(65, 80, 102), glow, 0.42 + (0.24 * pulse)) : (hovered || active ? RGB(65, 80, 102) : RGB(36, 42, 53));

    FillRound(dc, RECT{ rect.left + 1, rect.top + 2, rect.right + 1, rect.bottom + 2 }, RGB(8, 10, 14), RGB(8, 10, 14), kToolbarButtonRadius);
    if (hasGlow) {
        const BYTE softAlpha = static_cast<BYTE>(34 + (54 * pulse));
        const BYTE coreAlpha = static_cast<BYTE>(46 + (76 * pulse));
        FillRoundedAlpha(dc, RECT{ rect.left - 8, rect.top - 8, rect.right + 8, rect.bottom + 8 }, glow, static_cast<BYTE>(softAlpha / 2), static_cast<float>(kToolbarButtonRadius + 8));
        FillRoundedAlpha(dc, RECT{ rect.left - 5, rect.top - 5, rect.right + 5, rect.bottom + 5 }, glow, softAlpha, static_cast<float>(kToolbarButtonRadius + 5));
        FillRoundedAlpha(dc, RECT{ rect.left - 2, rect.top - 2, rect.right + 2, rect.bottom + 2 }, glow, coreAlpha, static_cast<float>(kToolbarButtonRadius + 2));
    }
    FillRound(dc, rect, topFill, border, kToolbarButtonRadius);

    const RECT inner = GdiDrawing::Inset(rect, 1);
    GdiDrawing::FillRectColor(dc, GdiDrawing::Inset(inner, 1), bottomFill);
    GdiDrawing::FillRectColor(dc, RECT{ inner.left + 4, inner.top + 1, inner.right - 4, inner.top + 2 }, pressed ? RGB(12, 15, 20) : BlendRatio(RGB(51, 60, 73), RGB(76, 90, 112), active ? 0.24 * pulse : 0.0));
    GdiDrawing::FillRectColor(dc, RECT{ inner.left + 4, inner.bottom - 2, inner.right - 4, inner.bottom - 1 }, pressed ? RGB(64, 72, 86) : RGB(12, 15, 20));

    if ((hovered || active) && !hasGlow) {
        const COLORREF soft = hasGlow ? BlendRatio(RGB(48, 70, 58), glow, 0.42 + (0.18 * pulse)) : (active ? BlendRatio(RGB(48, 61, 80), RGB(72, 92, 124), 0.18 * pulse) : RGB(46, 58, 76));
        const RECT innerGlow = GdiDrawing::Inset(rect, 2);
        StrokeRound(dc, innerGlow, soft, kToolbarButtonRadius - 2, 1);
    }

    if (!enabled) {
        GdiDrawing::FillRectColor(dc, GdiDrawing::Inset(rect, 2), RGB(24, 27, 33));
    }
}

} // namespace kb::editor

#endif
