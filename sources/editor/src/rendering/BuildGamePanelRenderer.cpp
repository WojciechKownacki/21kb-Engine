#include "rendering/BuildGamePanelRenderer.hpp"

#if defined(_WIN32)
#include "packaging/EditorProjectPackageService.hpp"
#include "rendering/BuildGamePanelLayout.hpp"
#include "rendering/BuildGamePanelModel.hpp"
#include "rendering/EditorPanelStyle.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "scene/EditorSceneContext.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {
namespace {

using panel_style::DrawDisclosureCaret;
using panel_style::DrawInputFrame;
using panel_style::DrawRowDivider;
using panel_style::DrawSectionCardOutline;
using panel_style::FillSectionHeaderBackground;
using panel_style::HoverFill;

constexpr COLORREF kBuildGreen = RGB(34, 108, 62);
constexpr COLORREF kBuildGreenEdge = RGB(48, 138, 82);

struct BuildProfile { std::string_view name; HeroIconKind icon; };
constexpr std::array<BuildProfile, 2> kBuildProfiles{{
    { "Development", HeroIconKind::CodeBracket },
    { "Release", HeroIconKind::RocketLaunch },
}};

[[nodiscard]] COLORREF Color(EditorColor color) noexcept { return GdiDrawing::ToColorRef(color); }

[[nodiscard]] COLORREF Blend(COLORREF a, COLORREF b, int percentB) noexcept {
    const int percentA = 100 - percentB;
    return RGB((GetRValue(a) * percentA + GetRValue(b) * percentB) / 100,
        (GetGValue(a) * percentA + GetGValue(b) * percentB) / 100,
        (GetBValue(a) * percentA + GetBValue(b) * percentB) / 100);
}

void DrawText(HDC dc, RECT rect, std::string_view text, COLORREF color, int pointSize = 12,
    int weight = FW_NORMAL, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    ScopedFont font{ pointSize, weight };
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text.data(), static_cast<int>(text.size()), &rect, static_cast<int>(flags | DT_NOPREFIX));
}

[[nodiscard]] HeroIconKind TargetIcon(const kb::packaging::PackagingTargetSpec& target) noexcept {
    switch (target.target) {
    case kb::packaging::PackagingTarget::WindowsX64: return HeroIconKind::PlatformWindows;
    case kb::packaging::PackagingTarget::LinuxX64: return HeroIconKind::PlatformLinux;
    case kb::packaging::PackagingTarget::AndroidAstcArm64:
    case kb::packaging::PackagingTarget::AndroidEtc2Arm64: return HeroIconKind::PlatformAndroid;
    case kb::packaging::PackagingTarget::WebGlWasm32:
    case kb::packaging::PackagingTarget::WebGpuWasm32: return HeroIconKind::Gamepad2;
    }
    return HeroIconKind::Cube;
}

[[nodiscard]] HeroIconKind SectionIcon(BuildGameSection section) noexcept {
    switch (section) {
    case BuildGameSection::Project: return HeroIconKind::WrenchScrewdriver;
    case BuildGameSection::Application: return HeroIconKind::Cube;
    case BuildGameSection::Content: return HeroIconKind::DocumentText;
    case BuildGameSection::Signing: return HeroIconKind::LockClosed;
    case BuildGameSection::Toolchain: return HeroIconKind::CommandLine;
    case BuildGameSection::Output: return HeroIconKind::Save;
    }
    return HeroIconKind::Cube;
}

void DrawSidebarCaption(HDC dc, const RECT& rect, std::string_view text, const EditorTheme& theme) {
    DrawText(dc, RECT{ rect.left + 12, rect.top, rect.right - 8, rect.bottom }, text,
        Color(theme.textDisabled), 10, FW_SEMIBOLD);
}

void DrawTargetRow(HDC dc, const RECT& row, const kb::packaging::PackagingTargetSpec& target,
    bool selected, bool hovered, const EditorTheme& theme) {
    if (!selected && hovered) GdiDrawing::FillRectColor(dc, row, HoverFill(theme));
    if (selected) {
        GdiDrawing::FillRectColor(dc, row, Blend(Color(theme.panel), Color(theme.accent), 18));
        GdiDrawing::FillRectColor(dc, RECT{ row.left, row.top, row.left + 3, row.bottom }, Color(theme.accent));
    }
    const RECT icon = BuildGamePanelLayout::IconBox(row, 18);
    HeroIconPainter::Draw(dc, icon, TargetIcon(target), Color(selected ? theme.textPrimary : theme.textSecondary));
    DrawText(dc, RECT{ icon.right + 10, row.top, row.right - 8, row.bottom }, target.displayName,
        Color(selected ? theme.textPrimary : theme.textSecondary), 12, selected ? FW_SEMIBOLD : FW_NORMAL);
}

void DrawProfileRow(HDC dc, const RECT& row, const BuildProfile& profile, bool selected,
    bool hovered, const EditorTheme& theme) {
    if (!selected && hovered) GdiDrawing::FillRectColor(dc, row, HoverFill(theme));
    if (selected) {
        GdiDrawing::FillRectColor(dc, row, Blend(Color(theme.panel), Color(theme.accent), 18));
        GdiDrawing::FillRectColor(dc, RECT{ row.left, row.top, row.left + 3, row.bottom }, Color(theme.accent));
    }
    const RECT icon = BuildGamePanelLayout::IconBox(row, 16);
    HeroIconPainter::Draw(dc, icon, profile.icon, Color(selected ? theme.textPrimary : theme.textSecondary));
    DrawText(dc, RECT{ icon.right + 10, row.top, row.right - 8, row.bottom }, profile.name,
        Color(selected ? theme.textPrimary : theme.textSecondary), 12, selected ? FW_SEMIBOLD : FW_NORMAL);
}

void DrawHeader(HDC dc, const RECT& header, const kb::packaging::PackagingTargetSpec& target,
    std::string_view profile, const EditorTheme& theme) {
    GdiDrawing::FillRectColor(dc, header, Blend(Color(theme.panel), Color(theme.chrome), 40));
    const RECT icon = BuildGamePanelLayout::IconBox(header, 26);
    HeroIconPainter::Draw(dc, icon, TargetIcon(target), Color(theme.accent));
    DrawText(dc, RECT{ icon.right + 12, header.top + 8, header.right - 140, header.top + 28 },
        target.displayName, Color(theme.textPrimary), 14, FW_SEMIBOLD);
    DrawText(dc, RECT{ icon.right + 12, header.top + 28, header.right - 140, header.bottom - 8 },
        target.summary, Color(theme.textSecondary), 11);
    DrawText(dc, RECT{ header.right - 132, header.top, header.right - 12, header.bottom }, profile,
        Color(theme.textSecondary), 11, FW_NORMAL, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

void DrawCheckbox(HDC dc, const RECT& box, bool checked, const EditorTheme& theme) {
    const int size = panel_style::kCheckboxSize;
    const int top = box.top + (((box.bottom - box.top) - size) / 2);
    const RECT check{ box.left, top, box.left + size, top + size };
    DrawInputFrame(dc, check, checked ? Color(theme.accent) : Color(theme.chrome), Color(theme.borderPanel));
    if (checked) {
        MoveToEx(dc, check.left + 3, check.top + (size / 2), nullptr);
        LineTo(dc, check.left + (size / 2), check.bottom - 3);
        LineTo(dc, check.right - 3, check.top + 3);
    }
}

[[nodiscard]] int DrawSection(HDC dc, const RECT& body, int y, int sectionIndex,
    const BuildGameSectionSpec& section, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    const bool collapsed = sceneContext.IsBuildGameSectionCollapsed(sectionIndex);
    const int visibleRows = collapsed ? 0 : static_cast<int>(section.rows.size());
    const bool headerHovered = sceneContext.BuildGameHoveredSection() == sectionIndex && sceneContext.BuildGameHoveredRow() < 0;
    const RECT header = BuildGamePanelLayout::SectionHeaderRow(body, y);
    DrawSectionCardOutline(dc, RECT{ body.left, header.top, body.right,
        header.bottom + (visibleRows * BuildGamePanelLayout::kOptionRowHeight) }, theme);
    FillSectionHeaderBackground(dc, header, headerHovered ? HoverFill(theme) : Color(theme.strip));
    const RECT caret = BuildGamePanelLayout::CaretBox(header);
    DrawDisclosureCaret(dc, caret, Color(theme.textSecondary), !collapsed);
    const RECT icon{ caret.right + 6, caret.top, caret.right + 21, caret.top + 15 };
    HeroIconPainter::Draw(dc, icon, SectionIcon(section.section), Color(theme.textSecondary));
    DrawText(dc, RECT{ icon.right + 9, header.top, header.right - 8, header.bottom },
        section.title, Color(theme.textPrimary), 12, FW_SEMIBOLD);
    y = header.bottom;
    if (collapsed) return y + BuildGamePanelLayout::kSectionSpacing;

    int rowIndex = 0;
    for (const BuildGameRowSpec& spec : section.rows) {
        const RECT row = BuildGamePanelLayout::OptionRow(body, y);
        if (sceneContext.BuildGameHoveredSection() == sectionIndex && sceneContext.BuildGameHoveredRow() == rowIndex)
            GdiDrawing::FillRectColor(dc, RECT{ row.left + 1, row.top, row.right - 1, row.bottom }, HoverFill(theme));
        DrawText(dc, BuildGamePanelLayout::OptionLabel(row), spec.label, Color(theme.textPrimary), 12);
        const RECT valueBox = BuildGamePanelLayout::OptionValueBox(row);
        if (spec.kind == BuildGameRowKind::Checkbox) {
            DrawCheckbox(dc, valueBox, sceneContext.BuildGameSettings().For(sceneContext.BuildGameTarget()).launchAfterBuild, theme);
        } else {
            const bool editing = sceneContext.IsBuildGameTextEditing() && sceneContext.BuildGameEditingField() == spec.field;
            DrawInputFrame(dc, valueBox, Color(theme.chrome), editing ? Color(theme.accent) : Color(theme.borderPanel));
            std::string value;
            if (spec.kind == BuildGameRowKind::Password) {
                const bool present = spec.field == BuildGameField::AndroidStorePassword
                    ? sceneContext.HasBuildGameStorePassword() : sceneContext.HasBuildGameKeyPassword();
                const std::size_t length = editing ? sceneContext.BuildGameEditBuffer().size() : (present ? 8U : 0U);
                value = length == 0U ? "Required for this Release build" : std::string(length, '*');
            } else {
                value = editing ? std::string{ sceneContext.BuildGameEditBuffer() } :
                    BuildGamePanelModel::Value(spec.field, sceneContext.BuildGameTarget(),
                        sceneContext.ProjectConfiguration(), sceneContext.BuildGameSettings());
            }
            DrawText(dc, RECT{ valueBox.left + 8, valueBox.top, valueBox.right - 8, valueBox.bottom }, value,
                spec.required && value.empty() ? RGB(220, 100, 85) : Color(theme.textPrimary), 12);
        }
        if (rowIndex + 1 < visibleRows) DrawRowDivider(dc, row, theme);
        y = row.bottom;
        ++rowIndex;
    }
    return y + BuildGamePanelLayout::kSectionSpacing;
}

void DrawFooter(HDC dc, const BuildGamePanelLayoutRects& rects, const EditorTheme& theme,
    const EditorSceneContext& sceneContext) {
    const EditorPackageSnapshot snapshot = sceneContext.BuildGamePackageSnapshot();
    const bool running = snapshot.state == EditorPackageJobState::Running;
    const BuildGameValidation validation = BuildGamePanelModel::Validate(sceneContext.BuildGameTarget(),
        sceneContext.ProjectConfiguration(), sceneContext.BuildGameSettings(),
        sceneContext.BuildGameSelectedProfile() == 1, running,
        sceneContext.HasBuildGameStorePassword(), sceneContext.HasBuildGameKeyPassword());
    const EditorBuildGameTargetSettings& targetSettings = sceneContext.BuildGameSettings().For(sceneContext.BuildGameTarget());
    std::string first = targetSettings.outputDirectory.empty() ? "Output directory is not selected" : targetSettings.outputDirectory.generic_string();
    std::string second = validation.reason;
    if (snapshot.state != EditorPackageJobState::Idle) {
        second = snapshot.status;
        if (running) second += " (" + std::to_string(snapshot.progress) + "%)";
        if (!snapshot.resultDirectory.empty()) first = snapshot.resultDirectory.generic_string();
        if (!snapshot.diagnostics.empty() && snapshot.diagnostics.back().severity != EditorPackageDiagnosticSeverity::Info) {
            if (snapshot.resultDirectory.empty()) first = snapshot.diagnostics.back().message;
            else second += " " + snapshot.diagnostics.back().message;
        }
    }
    DrawText(dc, RECT{ rects.status.left, rects.status.top + 6, rects.status.right, rects.status.top + 24 },
        first, Color(theme.textSecondary), 11);
    DrawText(dc, RECT{ rects.status.left, rects.status.top + 24, rects.status.right, rects.status.bottom - 6 },
        second, validation.canBuild || running ? Color(theme.textSecondary) : Color(theme.textDisabled), 11);
    const bool enabled = validation.canBuild || running;
    GdiDrawing::DrawSharpFrame(dc, rects.buildButton,
        enabled ? kBuildGreen : Blend(Color(theme.chrome), Color(theme.panel), 50),
        enabled ? kBuildGreenEdge : Color(theme.borderPanel));
    DrawText(dc, rects.buildButton, running ? "CANCEL" : "BUILD",
        enabled ? RGB(236, 245, 239) : Color(theme.textDisabled), 12, FW_SEMIBOLD,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

[[nodiscard]] std::vector<BuildGameSectionSpec> Sections(const EditorSceneContext& context) {
    return BuildGamePanelModel::Sections(context.BuildGameTarget(), context.BuildGameSelectedProfile() == 1);
}

} // namespace

int BuildGamePanelRenderer::SettingsContentHeight(const EditorSceneContext& sceneContext) noexcept {
    const std::vector<BuildGameSectionSpec> sections = Sections(sceneContext);
    std::vector<int> rows;
    rows.reserve(sections.size());
    for (std::size_t index = 0U; index < sections.size(); ++index)
        rows.push_back(sceneContext.IsBuildGameSectionCollapsed(static_cast<int>(index)) ? 0 :
            static_cast<int>(sections[index].rows.size()));
    return BuildGamePanelLayout::ContentHeight(rows);
}

BuildGamePanelRenderer::SidebarHit BuildGamePanelRenderer::HitTestSidebar(const RECT& content, int x, int y) {
    const BuildGamePanelLayoutRects rects = BuildGamePanelLayout::Resolve(content);
    SidebarHit hit{};
    if (x < rects.sidebar.left || x >= rects.sidebar.right) return hit;
    const auto targets = kb::packaging::PackagingTargets();
    for (int index = 0; index < static_cast<int>(targets.size()); ++index) {
        const RECT row = BuildGamePanelLayout::TargetRow(rects.platformsList, index);
        if (y >= row.top && y < row.bottom && row.bottom <= rects.platformsList.bottom) { hit.target = index; return hit; }
    }
    for (int index = 0; index < static_cast<int>(kBuildProfiles.size()); ++index) {
        const RECT row = BuildGamePanelLayout::ProfileRow(rects.profilesList, index);
        if (y >= row.top && y < row.bottom) { hit.profile = index; return hit; }
    }
    return hit;
}

BuildGamePanelRenderer::RowHit BuildGamePanelRenderer::HitTest(const RECT& content,
    const EditorSceneContext& sceneContext, int x, int y) {
    const BuildGamePanelLayoutRects rects = BuildGamePanelLayout::Resolve(content);
    if (x < rects.body.left || x >= rects.body.right || y < rects.body.top || y >= rects.body.bottom) return {};
    const int maxScroll = BuildGamePanelLayout::MaxScrollOffset(rects.body, SettingsContentHeight(sceneContext));
    int cursor = static_cast<int>(rects.body.top) - std::clamp(sceneContext.BuildGameScrollOffset(), 0, maxScroll);
    const std::vector<BuildGameSectionSpec> sections = Sections(sceneContext);
    for (int index = 0; index < static_cast<int>(sections.size()); ++index) {
        const int headerBottom = cursor + BuildGamePanelLayout::kSectionHeaderHeight;
        if (y >= cursor && y < headerBottom) return { index, -1 };
        cursor = headerBottom;
        if (!sceneContext.IsBuildGameSectionCollapsed(index)) {
            for (int row = 0; row < static_cast<int>(sections[static_cast<std::size_t>(index)].rows.size()); ++row) {
                const int rowBottom = cursor + BuildGamePanelLayout::kOptionRowHeight;
                if (y >= cursor && y < rowBottom) return { index, row };
                cursor = rowBottom;
            }
        }
        cursor += BuildGamePanelLayout::kSectionSpacing;
    }
    return {};
}

BuildGameField BuildGamePanelRenderer::FieldForHit(const EditorSceneContext& sceneContext, RowHit hit) noexcept {
    const std::vector<BuildGameSectionSpec> sections = Sections(sceneContext);
    if (hit.section < 0 || hit.section >= static_cast<int>(sections.size()) || hit.row < 0 ||
        hit.row >= static_cast<int>(sections[static_cast<std::size_t>(hit.section)].rows.size())) return BuildGameField::None;
    return sections[static_cast<std::size_t>(hit.section)].rows[static_cast<std::size_t>(hit.row)].field;
}

bool BuildGamePanelRenderer::HitTestBuildButton(const RECT& content, int x, int y) noexcept {
    const RECT button = BuildGamePanelLayout::Resolve(content).buildButton;
    return x >= button.left && x < button.right && y >= button.top && y < button.bottom;
}

void BuildGamePanelRenderer::Paint(HDC dc, const RECT& content, const EditorTheme& theme,
    EditorSceneContext& sceneContext) const {
    GdiDrawing::FillRectColor(dc, content, Color(theme.panel));
    const BuildGamePanelLayoutRects rects = BuildGamePanelLayout::Resolve(content);
    GdiDrawing::FillRectColor(dc, rects.sidebar, Color(theme.chrome));
    GdiDrawing::FillRectColor(dc, rects.divider, Color(theme.borderChrome));
    const auto targets = kb::packaging::PackagingTargets();
    const int selectedTarget = std::clamp(sceneContext.BuildGameSelectedTarget(), 0, static_cast<int>(targets.size()) - 1);
    const int selectedProfile = std::clamp(sceneContext.BuildGameSelectedProfile(), 0, 1);
    DrawSidebarCaption(dc, rects.platformsCaption, "PLATFORMS", theme);
    DrawSidebarCaption(dc, BuildGamePanelLayout::TargetGroupCaption(rects.platformsList, 0), "Player", theme);
    for (int index = 0; index < static_cast<int>(targets.size()); ++index)
        DrawTargetRow(dc, BuildGamePanelLayout::TargetRow(rects.platformsList, index), targets[static_cast<std::size_t>(index)],
            index == selectedTarget, sceneContext.BuildGameHoveredTarget() == index, theme);
    DrawSidebarCaption(dc, rects.profilesCaption, "BUILD PROFILES", theme);
    for (int index = 0; index < 2; ++index)
        DrawProfileRow(dc, BuildGamePanelLayout::ProfileRow(rects.profilesList, index), kBuildProfiles[static_cast<std::size_t>(index)],
            index == selectedProfile, sceneContext.BuildGameHoveredProfile() == index, theme);
    DrawHeader(dc, rects.header, targets[static_cast<std::size_t>(selectedTarget)],
        kBuildProfiles[static_cast<std::size_t>(selectedProfile)].name, theme);
    const std::vector<BuildGameSectionSpec> sections = Sections(sceneContext);
    const int contentHeight = SettingsContentHeight(sceneContext);
    const int scroll = std::clamp(sceneContext.BuildGameScrollOffset(), 0,
        BuildGamePanelLayout::MaxScrollOffset(rects.body, contentHeight));
    const int savedDc = SaveDC(dc);
    IntersectClipRect(dc, rects.body.left, rects.body.top, rects.body.right, rects.body.bottom);
    int y = static_cast<int>(rects.body.top) - scroll;
    for (int index = 0; index < static_cast<int>(sections.size()); ++index)
        y = DrawSection(dc, rects.body, y, index, sections[static_cast<std::size_t>(index)], theme, sceneContext);
    RestoreDC(dc, savedDc);
    const RECT track = BuildGamePanelLayout::ScrollbarTrack(rects.body, contentHeight);
    if (track.right > track.left) {
        GdiDrawing::FillRectColor(dc, track, Blend(Color(theme.panel), Color(theme.background), 45));
        GdiDrawing::FillRectColor(dc, BuildGamePanelLayout::ScrollbarThumb(rects.body, contentHeight, scroll),
            Blend(Color(theme.panel), Color(theme.textDisabled), 55));
    }
    DrawFooter(dc, rects, theme, sceneContext);
}

} // namespace kb::editor
#endif
