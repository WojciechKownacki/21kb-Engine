#include "rendering/BuildGamePanelRenderer.hpp"

#if defined(_WIN32)
#include "rendering/BuildGamePanelLayout.hpp"
#include "rendering/EditorPanelStyle.hpp"
#include "rendering/GdiDrawing.hpp"
#include "scene/EditorSceneContext.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace kb::editor {
namespace {

// Every rounded frame, colour and row metric comes from the shared panel style, so
// this panel and the Inspector stay one look rather than two that drift.
using panel_style::DrawInputFrame;
using panel_style::DrawSectionCardOutline;
using panel_style::FillSectionHeaderBackground;
using panel_style::HoverFill;

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

// The build action reads as a commit, so it keeps its own green rather than the editor
// accent, which is spent on selection everywhere else in the shell.
constexpr COLORREF kBuildGreen = RGB(34, 108, 62);
constexpr COLORREF kBuildGreenEdge = RGB(48, 138, 82);

void DrawText(
    HDC dc,
    RECT rect,
    std::string_view text,
    COLORREF color,
    int pointSize = 12,
    int weight = FW_NORMAL,
    UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    ScopedFont font{ pointSize, weight };
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text.data(), static_cast<int>(text.size()), &rect, static_cast<int>(flags | DT_NOPREFIX));
}

struct BuildTarget {
    std::string_view name;
    std::string_view summary;
    HeroIconKind icon;
};

// The five targets the panel offers, split into the two groups the sidebar shows.
constexpr std::array<BuildTarget, 3> kPlayerTargets{{
    { "Windows 64 bit", "Game", HeroIconKind::PlatformWindows },
    { "Android ASTC", "Player package  |  arm64-v8a  |  APK", HeroIconKind::PlatformAndroid },
    { "Linux 64 bit", "Player package  |  x86-64  |  Portable folder", HeroIconKind::PlatformLinux },
}};

constexpr std::array<BuildTarget, 2> kServerTargets{{
    { "Windows Dedicated Server", "Dedicated server  |  x86-64  |  Portable folder", HeroIconKind::Server },
    { "Linux Dedicated Server", "Dedicated server  |  x86-64  |  Portable folder", HeroIconKind::Server },
}};

struct BuildProfile {
    std::string_view name;
    HeroIconKind icon;
};

constexpr std::array<BuildProfile, 2> kBuildProfiles{{
    { "Development", HeroIconKind::CodeBracket },
    { "Release", HeroIconKind::RocketLaunch },
}};

struct OptionRowSpec {
    std::string_view label;
    std::string_view value;
};

struct OptionSection {
    std::string_view title;
    HeroIconKind icon;
    std::span<const OptionRowSpec> rows;
};

// Values are the unconfigured defaults: the panel states what a build will ask for
// before anything is wired to answer it.
constexpr std::array<OptionRowSpec, 5> kProjectRows{{
    { "Project name", "Not configured" },
    { "Description", "Not configured" },
    { "Publisher", "Not configured" },
    { "Version", "Not configured" },
    { "Build profile", "Development" },
}};

constexpr std::array<OptionRowSpec, 5> kApplicationRows{{
    { "Product name", "Not configured" },
    { "Executable name", "Not configured" },
    { "File version", "Not configured" },
    { "Publisher", "Not configured" },
    { "Application icon", "Not configured" },
}};

constexpr std::array<OptionRowSpec, 1> kContentRows{{
    { "Startup map", "Not configured" },
}};

constexpr std::array<OptionRowSpec, 1> kSigningRows{{
    { "Signing mode", "Unsigned" },
}};

constexpr std::array<OptionRowSpec, 2> kOutputRows{{
    { "Output directory", "Select folder..." },
    { "Launch after build", "" },
}};

void DrawSidebarCaption(HDC dc, const RECT& rect, std::string_view text, const EditorTheme& theme) {
    DrawText(dc, RECT{ rect.left + 12, rect.top, rect.right - 8, rect.bottom }, text,
        Color(theme.textDisabled), 10, FW_SEMIBOLD);
}

void DrawTargetRow(
    HDC dc,
    const RECT& row,
    const BuildTarget& target,
    bool selected,
    const EditorTheme& theme) {
    if (selected) {
        GdiDrawing::FillRectColor(dc, row, Blend(Color(theme.panel), Color(theme.accent), 18));
        GdiDrawing::FillRectColor(dc, RECT{ row.left, row.top, row.left + 3, row.bottom }, Color(theme.accent));
    }
    const RECT icon = BuildGamePanelLayout::IconBox(row, 18);
    HeroIconPainter::Draw(dc, icon, target.icon,
        Color(selected ? theme.textPrimary : theme.textSecondary));
    const RECT label{ icon.right + 10, row.top, row.right - 8, row.bottom };
    DrawText(dc, label, target.name, Color(selected ? theme.textPrimary : theme.textSecondary),
        12, selected ? FW_SEMIBOLD : FW_NORMAL);
}

void DrawProfileRow(HDC dc, const RECT& row, const BuildProfile& profile, bool selected, const EditorTheme& theme) {
    if (selected) {
        GdiDrawing::FillRectColor(dc, row, Blend(Color(theme.panel), Color(theme.accent), 18));
        GdiDrawing::FillRectColor(dc, RECT{ row.left, row.top, row.left + 3, row.bottom }, Color(theme.accent));
    }
    const RECT icon = BuildGamePanelLayout::IconBox(row, 16);
    HeroIconPainter::Draw(dc, icon, profile.icon,
        Color(selected ? theme.textPrimary : theme.textSecondary));
    const RECT label{ icon.right + 10, row.top, row.right - 8, row.bottom };
    DrawText(dc, label, profile.name, Color(selected ? theme.textPrimary : theme.textSecondary),
        12, selected ? FW_SEMIBOLD : FW_NORMAL);
}

void DrawHeader(HDC dc, const RECT& header, const BuildTarget& target, const EditorTheme& theme) {
    GdiDrawing::FillRectColor(dc, header, Blend(Color(theme.panel), Color(theme.chrome), 40));
    const RECT icon = BuildGamePanelLayout::IconBox(header, 26);
    HeroIconPainter::Draw(dc, icon, target.icon, Color(theme.accent));
    const RECT title{ icon.right + 12, header.top + 8, header.right - 140, header.top + 28 };
    DrawText(dc, title, target.name, Color(theme.textPrimary), 14, FW_SEMIBOLD);
    const RECT subtitle{ icon.right + 12, header.top + 28, header.right - 140, header.bottom - 8 };
    DrawText(dc, subtitle, target.summary, Color(theme.textSecondary), 11);
    const RECT profile{ header.right - 132, header.top, header.right - 12, header.bottom };
    DrawText(dc, profile, kBuildProfiles[0].name, Color(theme.textSecondary), 11, FW_NORMAL,
        DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

void DrawCheckbox(HDC dc, const RECT& box, const EditorTheme& theme) {
    const int size = panel_style::kCheckboxSize;
    const int top = box.top + (((box.bottom - box.top) - size) / 2);
    DrawInputFrame(dc, RECT{ box.left, top, box.left + size, top + size },
        Color(theme.chrome), Color(theme.borderPanel));
}

// One settings section: an uppercase caption with its glyph, then its rows. Returns the
// y the next section starts at.
[[nodiscard]] int DrawSection(
    HDC dc,
    const RECT& body,
    int y,
    const OptionSection& section,
    const EditorTheme& theme) {
    const RECT header = BuildGamePanelLayout::SectionHeaderRow(body, y);
    const RECT card{ body.left, header.top, body.right,
        header.bottom + (static_cast<int>(section.rows.size()) * BuildGamePanelLayout::kOptionRowHeight) };
    DrawSectionCardOutline(dc, card, theme);
    FillSectionHeaderBackground(dc, header, Color(theme.strip));
    const RECT icon = BuildGamePanelLayout::IconBox(header, 15);
    HeroIconPainter::Draw(dc, icon, section.icon, Color(theme.textSecondary));
    DrawText(dc, RECT{ icon.right + 9, header.top, header.right - 8, header.bottom },
        section.title, Color(theme.textPrimary), 12, FW_SEMIBOLD);
    y = header.bottom;

    for (const OptionRowSpec& spec : section.rows) {
        const RECT row = BuildGamePanelLayout::OptionRow(body, y);
        DrawText(dc, BuildGamePanelLayout::OptionLabel(row), spec.label, Color(theme.textPrimary), 12);
        const RECT value = BuildGamePanelLayout::OptionValueBox(row);
        if (spec.value.empty()) {
            DrawCheckbox(dc, value, theme);
        } else {
            DrawInputFrame(dc, value, Color(theme.chrome), Color(theme.borderPanel));
            DrawText(dc, RECT{ value.left + 8, value.top, value.right - 8, value.bottom },
                spec.value, Color(theme.textPrimary), 12);
        }
        y = row.bottom;
    }
    return y + BuildGamePanelLayout::kSectionSpacing;
}

void DrawFooter(HDC dc, const BuildGamePanelLayoutRects& rects, const EditorTheme& theme) {
    const RECT first{ rects.status.left, rects.status.top + 6, rects.status.right, rects.status.top + 24 };
    const RECT second{ rects.status.left, rects.status.top + 24, rects.status.right, rects.status.bottom - 6 };
    DrawText(dc, first, "Output directory is not selected", Color(theme.textSecondary), 11);
    DrawText(dc, second, "Ready to build Windows x64.", Color(theme.textDisabled), 11);

    GdiDrawing::DrawSharpFrame(dc, rects.buildButton, kBuildGreen, kBuildGreenEdge);
    DrawText(dc, rects.buildButton, "BUILD", RGB(236, 245, 239), 12, FW_SEMIBOLD,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

[[nodiscard]] std::array<int, 5> SectionRowCounts() noexcept {
    return {
        static_cast<int>(kProjectRows.size()),
        static_cast<int>(kApplicationRows.size()),
        static_cast<int>(kContentRows.size()),
        static_cast<int>(kSigningRows.size()),
        static_cast<int>(kOutputRows.size()),
    };
}

} // namespace

int BuildGamePanelRenderer::SettingsContentHeight() noexcept {
    const std::array<int, 5> rows = SectionRowCounts();
    return BuildGamePanelLayout::ContentHeight(std::span<const int>{ rows });
}

void BuildGamePanelRenderer::Paint(
    HDC dc,
    const RECT& content,
    const EditorTheme& theme,
    EditorSceneContext& sceneContext) const {
    GdiDrawing::FillRectColor(dc, content, Color(theme.panel));
    const BuildGamePanelLayoutRects rects = BuildGamePanelLayout::Resolve(content);

    GdiDrawing::FillRectColor(dc, rects.sidebar, Color(theme.chrome));
    GdiDrawing::FillRectColor(dc, rects.divider, Color(theme.borderChrome));
    DrawSidebarCaption(dc, rects.platformsCaption, "PLATFORMS", theme);
    DrawSidebarCaption(dc, BuildGamePanelLayout::TargetGroupCaption(rects.platformsList, 0), "Player", theme);
    for (int index = 0; index < static_cast<int>(kPlayerTargets.size()); ++index) {
        DrawTargetRow(dc, BuildGamePanelLayout::TargetRow(rects.platformsList, index),
            kPlayerTargets[static_cast<std::size_t>(index)], index == 0, theme);
    }
    DrawSidebarCaption(dc, BuildGamePanelLayout::TargetGroupCaption(rects.platformsList, 1), "Dedicated server", theme);
    for (int index = 0; index < static_cast<int>(kServerTargets.size()); ++index) {
        DrawTargetRow(dc, BuildGamePanelLayout::TargetRow(rects.platformsList, index + 3),
            kServerTargets[static_cast<std::size_t>(index)], false, theme);
    }

    DrawSidebarCaption(dc, rects.profilesCaption, "BUILD PROFILES", theme);
    for (int index = 0; index < static_cast<int>(kBuildProfiles.size()); ++index) {
        DrawProfileRow(dc, BuildGamePanelLayout::ProfileRow(rects.profilesList, index),
            kBuildProfiles[static_cast<std::size_t>(index)], index == 0, theme);
    }

    DrawHeader(dc, rects.header, kPlayerTargets[0], theme);

    const std::array<OptionSection, 5> sections{{
        { "PROJECT", HeroIconKind::WrenchScrewdriver, kProjectRows },
        { "WINDOWS APPLICATION", HeroIconKind::Cube, kApplicationRows },
        { "CONTENT", HeroIconKind::DocumentText, kContentRows },
        { "SIGNING", HeroIconKind::LockClosed, kSigningRows },
        { "OUTPUT", HeroIconKind::Save, kOutputRows },
    }};
    const int contentHeight = SettingsContentHeight();
    const int maxScroll = BuildGamePanelLayout::MaxScrollOffset(rects.body, contentHeight);
    const int scroll = std::clamp(sceneContext.BuildGameScrollOffset(), 0, maxScroll);

    // The column is taller than the leaf, so it scrolls inside its own clip. Rows are laid
    // out from a negative origin rather than being skipped, so a half-scrolled row is cut
    // by the clip instead of vanishing.
    const int savedDc = SaveDC(dc);
    IntersectClipRect(dc, rects.body.left, rects.body.top, rects.body.right, rects.body.bottom);
    int y = rects.body.top - scroll;
    for (const OptionSection& section : sections) {
        y = DrawSection(dc, rects.body, y, section, theme);
    }
    RestoreDC(dc, savedDc);

    const RECT track = BuildGamePanelLayout::ScrollbarTrack(rects.body, contentHeight);
    if (track.right > track.left) {
        GdiDrawing::FillRectColor(dc, track, Blend(Color(theme.panel), Color(theme.background), 45));
        GdiDrawing::FillRectColor(dc,
            BuildGamePanelLayout::ScrollbarThumb(rects.body, contentHeight, scroll),
            Blend(Color(theme.panel), Color(theme.textDisabled), 55));
    }

    DrawFooter(dc, rects, theme);
}

} // namespace kb::editor

#endif
