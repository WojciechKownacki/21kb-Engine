#include "rendering/PanelContentRenderer.hpp"

#if defined(_WIN32)
#include "console/EditorConsoleLayout.hpp"
#include "rendering/ConsoleDetailTextOverlay.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HierarchyPanelRenderer.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/EditorScriptEditorOverlay.hpp"
#include "rendering/AnimationClipEditorPanelRenderer.hpp"
#include "rendering/AnimatorEditorPanelRenderer.hpp"
#include "rendering/InspectorPanelRenderer.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "rendering/ParticleEditorPanelRenderer.hpp"
#include "rendering/PluginsPanelRenderer.hpp"
#include "rendering/ProjectFilesPanelRenderer.hpp"
#include "rendering/ProjectSettingsPanelRenderer.hpp"
#include "rendering/EditorSettingsPanelRenderer.hpp"
#include "rendering/ScriptEditorPanelRenderer.hpp"
#include "rendering/SkeletalMeshEditorPanelRenderer.hpp"
#include "rendering/ScenePanelContentRenderer.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <algorithm>
#include <array>
#include <ctime>
#include <string>

namespace kb::editor {
namespace {

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

[[nodiscard]] RECT IntersectRectOrEmpty(const RECT& a, const RECT& b) noexcept {
    RECT clipped{};
    if (IntersectRect(&clipped, &a, &b) == 0) {
        return {};
    }
    return clipped;
}

[[nodiscard]] const char* LevelLabel(EditorConsoleLevel level) noexcept {
    switch (level) {
    case EditorConsoleLevel::Info:
        return "i";
    case EditorConsoleLevel::Warning:
        return "!";
    case EditorConsoleLevel::Error:
        return "x";
    }
    return "i";
}

[[nodiscard]] const char* LongLevelLabel(EditorConsoleLevel level) noexcept {
    switch (level) {
    case EditorConsoleLevel::Info:
        return "Info";
    case EditorConsoleLevel::Warning:
        return "Warning";
    case EditorConsoleLevel::Error:
        return "Error";
    }
    return "Info";
}

[[nodiscard]] COLORREF LevelColor(EditorConsoleLevel level) noexcept {
    switch (level) {
    case EditorConsoleLevel::Info:
        return RGB(159, 197, 232);
    case EditorConsoleLevel::Warning:
        return RGB(232, 197, 106);
    case EditorConsoleLevel::Error:
        return RGB(232, 112, 112);
    }
    return RGB(159, 197, 232);
}

[[nodiscard]] COLORREF PillBackground(const EditorTheme& theme, EditorConsoleLevel level, bool active) noexcept {
    if (!active) {
        return Color(theme.chrome);
    }
    switch (level) {
    case EditorConsoleLevel::Info:
        return RGB(26, 51, 80);
    case EditorConsoleLevel::Warning:
        return RGB(59, 46, 13);
    case EditorConsoleLevel::Error:
        return RGB(59, 18, 18);
    }
    return RGB(26, 51, 80);
}

[[nodiscard]] COLORREF PillBorder(const EditorTheme& theme, EditorConsoleLevel level, bool active) noexcept {
    if (!active) {
        return Color(theme.borderPanel);
    }
    switch (level) {
    case EditorConsoleLevel::Info:
        return RGB(46, 95, 138);
    case EditorConsoleLevel::Warning:
        return RGB(122, 92, 26);
    case EditorConsoleLevel::Error:
        return RGB(122, 34, 34);
    }
    return RGB(46, 95, 138);
}

[[nodiscard]] COLORREF RowBackground(const EditorTheme& theme, EditorConsoleLevel level, bool zebra) noexcept {
    const COLORREF base = zebra ? Blend(Color(theme.panel), Color(theme.strip), 26) : Color(theme.panel);
    switch (level) {
    case EditorConsoleLevel::Warning:
        return Blend(base, LevelColor(level), 7);
    case EditorConsoleLevel::Error:
        return Blend(base, LevelColor(level), 7);
    case EditorConsoleLevel::Info:
    default:
        return base;
    }
}

[[nodiscard]] COLORREF MessageColor(EditorConsoleLevel level) noexcept {
    switch (level) {
    case EditorConsoleLevel::Warning:
        return RGB(212, 168, 75);
    case EditorConsoleLevel::Error:
        return RGB(212, 106, 106);
    case EditorConsoleLevel::Info:
    default:
        return RGB(196, 205, 214);
    }
}

[[nodiscard]] std::string FormatCount(std::uint32_t count) {
    return count > 999U ? "999+" : std::to_string(count);
}

[[nodiscard]] std::string FormatTimestamp(std::uint64_t ms) {
    const std::time_t seconds = static_cast<std::time_t>(ms / 1000U);
    std::tm local{};
    localtime_s(&local, &seconds);

    char buffer[16]{};
    if (std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local) == 0) {
        return "00:00:00";
    }
    return std::string{ buffer };
}

void DrawText(HDC dc, RECT rect, const char* text, COLORREF color, int pointSize = 12, int weight = FW_NORMAL, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    ScopedFont font{ pointSize, weight };
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, static_cast<int>(flags | DT_NOPREFIX));
}

void DrawPill(HDC dc, RECT rect, const EditorTheme& theme, EditorConsoleLevel level, std::uint32_t count, bool active) {
    GdiDrawing::DrawSharpFrame(dc, rect, PillBackground(theme, level, active), PillBorder(theme, level, active));

    RECT icon{ rect.left + 6, rect.top, rect.left + 16, rect.bottom };
    DrawText(dc, icon, LevelLabel(level), active ? LevelColor(level) : Color(theme.textDisabled), 12, FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT countRect{ icon.right + 4, rect.top, rect.right - 6, rect.bottom };
    DrawText(dc, countRect, FormatCount(count).c_str(), active ? Color(theme.textPrimary) : Color(theme.textDisabled), 12, FW_NORMAL, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawButton(HDC dc, RECT rect, const EditorTheme& theme, const char* label, bool hovered = false, bool pressed = false) {
    const COLORREF background = pressed ? Color(theme.tabActive) : (hovered ? Blend(Color(theme.panel), Color(theme.accent), 9) : Color(theme.chrome));
    const COLORREF border = pressed || hovered ? Color(theme.accent) : Color(theme.borderPanel);
    const COLORREF text = pressed ? Color(theme.textPrimary) : Color(theme.textSecondary);
    GdiDrawing::DrawSharpFrame(dc, rect, background, border);
    if (pressed) {
        GdiDrawing::FillRectColor(dc, RECT{ rect.left + 1, rect.top + 1, rect.right - 1, rect.top + 2 }, Color(theme.chrome));
    }
    RECT textRect = EditorConsoleInsetRect(rect, 8, 0);
    if (pressed) {
        ++textRect.top;
        ++textRect.bottom;
    }
    DrawText(dc, textRect, label, text, 12, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawConsoleToolbar(HDC dc, const EditorConsoleLayoutRects& layout, const EditorTheme& theme, const EditorConsoleState& console) {
    GdiDrawing::FillRectColor(dc, layout.toolbar, Color(theme.strip));
    GdiDrawing::FillRectColor(dc, RECT{ layout.toolbar.left, layout.toolbar.bottom - 1, layout.toolbar.right, layout.toolbar.bottom }, Color(theme.borderChrome));
    DrawPill(dc, layout.infoButton, theme, EditorConsoleLevel::Info, console.Count(EditorConsoleLevel::Info), console.ShowInfo());
    DrawPill(dc, layout.warningButton, theme, EditorConsoleLevel::Warning, console.Count(EditorConsoleLevel::Warning), console.ShowWarnings());
    DrawPill(dc, layout.errorButton, theme, EditorConsoleLevel::Error, console.Count(EditorConsoleLevel::Error), console.ShowErrors());
    DrawButton(dc, layout.copyLineButton, theme, "Copy Line", console.HoveredButton() == EditorConsoleButton::CopyLine, console.PressedButton() == EditorConsoleButton::CopyLine);
    DrawButton(dc, layout.saveLogButton, theme, "Save Log", console.HoveredButton() == EditorConsoleButton::SaveLog, console.PressedButton() == EditorConsoleButton::SaveLog);
    DrawButton(dc, layout.clearButton, theme, "Clear", console.HoveredButton() == EditorConsoleButton::Clear, console.PressedButton() == EditorConsoleButton::Clear);
}

void DrawConsoleRow(HDC dc, RECT row, const EditorTheme& theme, const EditorConsoleEntry& entry, int visibleIndex, bool selected) {
    const bool zebra = (visibleIndex & 1) != 0;
    GdiDrawing::FillRectColor(dc, row, selected ? Blend(Color(theme.panel), Color(theme.accent), 16) : RowBackground(theme, entry.level, zebra));
    if (selected) {
        GdiDrawing::FillRectColor(dc, RECT{ row.left, row.top, row.left + 3, row.bottom }, Color(theme.accent));
    }

    const std::string time = FormatTimestamp(entry.timestampMs);
    DrawText(dc, RECT{ row.left + 10, row.top, row.left + 90, row.bottom }, time.c_str(), Color(theme.textDisabled), 12);

    RECT category{ row.left + 98, row.top + 3, row.left + 222, row.bottom - 3 };
    GdiDrawing::FillRectColor(dc, category, Color(theme.chrome));
    DrawText(dc, EditorConsoleInsetRect(category, 6, 0), entry.category.c_str(), Color(theme.textSecondary), 11, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    DrawText(dc, RECT{ row.left + 230, row.top, row.right - 10, row.bottom }, entry.message.c_str(), MessageColor(entry.level), 12);

    GdiDrawing::FillRectColor(dc, RECT{ row.left, row.bottom - 1, row.right, row.bottom }, Color(theme.borderChrome));
}

void DrawConsoleDetailScrollbar(HDC dc, const EditorConsoleLayoutRects& layout, const EditorTheme& theme, const EditorConsoleState& console) {
    const EditorConsoleDetailScrollMetrics scroll = ResolveEditorConsoleDetailScrollMetrics(layout.detailTextArea, console);
    if (scroll.maxLine <= 0) {
        return;
    }

    GdiDrawing::DrawSharpFrame(dc, layout.detailScrollbarTrack, Color(theme.chrome), Color(theme.borderChrome));
    const COLORREF thumbColor = Color(console.IsDetailScrollbarDragging() ? theme.accent : theme.borderPanel);
    const COLORREF thumbBorder = Color(console.IsDetailScrollbarDragging() ? theme.textSecondary : theme.borderPanel);
    GdiDrawing::DrawSharpFrame(dc, layout.detailScrollbarThumb, thumbColor, thumbBorder);
    const int midY = layout.detailScrollbarThumb.top + (layout.detailScrollbarThumb.bottom - layout.detailScrollbarThumb.top) / 2;
    GdiDrawing::FillRectColor(dc, RECT{ layout.detailScrollbarThumb.left + 2, midY - 2, layout.detailScrollbarThumb.right - 2, midY - 1 }, Color(theme.textSecondary));
    GdiDrawing::FillRectColor(dc, RECT{ layout.detailScrollbarThumb.left + 2, midY + 1, layout.detailScrollbarThumb.right - 2, midY + 2 }, Color(theme.textSecondary));
}

void DrawConsoleListScrollbar(HDC dc, const EditorConsoleLayoutRects& layout, const EditorTheme& theme, const EditorConsoleState& console) {
    const EditorConsoleListScrollMetrics scroll = ResolveEditorConsoleListScrollMetrics(layout.listRows, console);
    if (scroll.maxRow <= 0) {
        return;
    }

    GdiDrawing::DrawSharpFrame(dc, layout.listScrollbarTrack, Color(theme.chrome), Color(theme.borderChrome));
    const COLORREF thumbColor = Color(console.IsListScrollbarDragging() ? theme.accent : theme.borderPanel);
    const COLORREF thumbBorder = Color(console.IsListScrollbarDragging() ? theme.textSecondary : theme.borderPanel);
    GdiDrawing::DrawSharpFrame(dc, layout.listScrollbarThumb, thumbColor, thumbBorder);
}

void DrawConsoleDetail(HDC dc, const EditorConsoleLayoutRects& layout, const EditorTheme& theme, const EditorConsoleState& console) {
    const EditorConsoleEntry* selected = console.SelectedEntry();
    if (selected == nullptr) {
        return;
    }

    GdiDrawing::FillRectColor(dc, layout.detailSplitter, Color(theme.panel));
    GdiDrawing::FillRectColor(dc, layout.detailSplitterVisual, Color(console.IsDetailResizeDragging() ? theme.accent : theme.borderChrome));
    GdiDrawing::DrawSharpFrame(dc, layout.detail, Color(theme.chrome), Color(theme.borderChrome));
    GdiDrawing::DrawSharpFrame(dc, layout.detailHeader, Color(theme.panel), Color(theme.borderChrome));

    DrawText(dc, RECT{ layout.detailHeader.left + 10, layout.detailHeader.top, layout.detailHeader.left + 88, layout.detailHeader.bottom }, LongLevelLabel(selected->level), LevelColor(selected->level), 12, FW_SEMIBOLD);
    DrawText(dc, RECT{ layout.detailHeader.left + 92, layout.detailHeader.top, layout.detailHeader.left + 220, layout.detailHeader.bottom }, selected->category.c_str(), Color(theme.textSecondary), 11);
    const std::string time = FormatTimestamp(selected->timestampMs);
    DrawText(dc, RECT{ layout.detailHeader.left + 224, layout.detailHeader.top, layout.detailHeader.right - 10, layout.detailHeader.bottom }, time.c_str(), Color(theme.textDisabled), 11);
    DrawConsoleDetailScrollbar(dc, layout, theme, console);
}

void DrawConsolePanel(HDC dc, const RECT& content, const EditorTheme& theme, const EditorConsoleState& console) {
    const EditorConsoleLayoutRects layout = ResolveEditorConsoleLayout(content, console);
    GdiDrawing::FillRectColor(dc, content, Color(theme.panel));
    DrawConsoleToolbar(dc, layout, theme, console);

    constexpr int rowHeight = 22;
    const EditorConsoleListScrollMetrics scroll = ResolveEditorConsoleListScrollMetrics(layout.listRows, console);
    const int firstVisible = std::clamp(console.ListScrollRow(), 0, scroll.maxRow);
    const int lastVisible = firstVisible + scroll.visibleRows + 1;
    int rowIndex = 0;
    int accepted = 0;
    int acceptedIndex = 0;
    for (const EditorConsoleEntry& entry : console.Entries()) {
        if (!console.Accepts(entry.level)) {
            continue;
        }
        ++accepted;
        if (acceptedIndex < firstVisible) {
            ++acceptedIndex;
            continue;
        }
        if (acceptedIndex >= lastVisible) {
            break;
        }
        RECT row{ layout.listRows.left, layout.listRows.top + rowIndex * rowHeight, layout.listRows.right, layout.listRows.top + (rowIndex + 1) * rowHeight };
        DrawConsoleRow(dc, row, theme, entry, rowIndex, console.SelectedSequence() == entry.sequence);
        ++rowIndex;
        ++acceptedIndex;
    }

    if (accepted == 0) {
        DrawText(dc, layout.listRows, console.Entries().empty() ? "Console is ready. No log entries yet." : "No log entries match the active filters.", Color(theme.textDisabled), 12, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    DrawConsoleListScrollbar(dc, layout, theme, console);
    DrawConsoleDetail(dc, layout, theme, console);
}

} // namespace

void PanelContentRenderer::Paint(
    HDC dc,
    const RECT& content,
    const RECT& panelFrame,
    const RECT& contentClip,
    const RECT& overlayBounds,
    const DockPanel& panel,
    const EditorTheme& theme,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& renderBackendSettings,
    bool floating,
    EditorSceneBgfxViewport* sceneViewport,
    HWND sceneViewportHost) const {
    static_cast<void>(panelFrame);
    static_cast<void>(metrics);
    static_cast<void>(floating);

    const int savedDc = SaveDC(dc);
    IntersectClipRect(dc, contentClip.left, contentClip.top, contentClip.right, contentClip.bottom);
    const RECT visibleContent = IntersectRectOrEmpty(content, contentClip);

    switch (panel.kind) {
    case DockPanelKind::Hierarchy:
        HierarchyPanelRenderer{}.Paint(dc, content, theme, sceneContext);
        break;
    case DockPanelKind::Inspector:
        InspectorPanelRenderer{}.Paint(dc, content, theme, sceneContext);
        break;
    case DockPanelKind::MaterialEditor:
        MaterialEditorPanelRenderer{}.Paint(dc, visibleContent, theme, sceneContext);
        break;
    case DockPanelKind::SkeletalMeshEditor:
        SkeletalMeshEditorPanelRenderer{}.Paint(
            dc, sceneViewportHost, visibleContent, panel, theme, sceneContext,
            renderBackendSettings, sceneViewport);
        break;
    case DockPanelKind::AnimationClipEditor:
        AnimationClipEditorPanelRenderer{}.Paint(
            dc, sceneViewportHost, visibleContent, panel, theme, sceneContext,
            renderBackendSettings, sceneViewport);
        break;
    case DockPanelKind::AnimatorEditor:
        AnimatorEditorPanelRenderer{}.Paint(
            dc, sceneViewportHost, visibleContent, panel, theme, sceneContext,
            renderBackendSettings, sceneViewport);
        break;
    case DockPanelKind::ParticleEditor:
        ParticleEditorPanelRenderer{}.Paint(
            dc, sceneViewportHost, visibleContent, panel, theme, sceneContext,
            renderBackendSettings, sceneViewport);
        break;
    case DockPanelKind::Assets:
        ProjectFilesPanelRenderer{}.Paint(dc, content, overlayBounds, theme, sceneContext);
        break;
    case DockPanelKind::Console:
        DrawConsolePanel(dc, content, theme, sceneContext.Console());
        ConsoleDetailTextOverlay::Sync(sceneViewportHost, content, sceneContext.Console());
        break;
    case DockPanelKind::Scene:
        ScenePanelContentRenderer{}.Paint(dc, content, panel, theme, sceneContext, renderBackendSettings, sceneViewport, sceneViewportHost);
        break;
    case DockPanelKind::ProjectSettings:
        ProjectSettingsPanelRenderer{}.Paint(dc, content, theme, sceneContext, renderBackendSettings);
        break;
    case DockPanelKind::EditorSettings:
        EditorSettingsPanelRenderer{}.Paint(dc, content, theme, sceneContext);
        break;
    case DockPanelKind::Plugins:
        PluginsPanelRenderer{}.Paint(dc, content, theme, sceneContext);
        break;
    case DockPanelKind::ScriptEditor:
        ScriptEditorPanelRenderer{}.Paint(dc, content, theme, sceneContext, EditorScriptEditorOverlay::IsDirty(sceneViewportHost));
        EditorScriptEditorOverlay::Sync(sceneViewportHost, content, sceneContext);
        break;
    case DockPanelKind::Generic:
    default:
        break;
    }

    RestoreDC(dc, savedDc);

}

} // namespace kb::editor

#endif
