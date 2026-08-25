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

[[nodiscard]] COLORREF PillBackground(EditorConsoleLevel level, bool active) noexcept {
    if (!active) {
        return RGB(34, 37, 42);
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

[[nodiscard]] COLORREF PillBorder(EditorConsoleLevel level, bool active) noexcept {
    if (!active) {
        return RGB(58, 61, 66);
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

[[nodiscard]] COLORREF RowBackground(EditorConsoleLevel level, bool zebra) noexcept {
    switch (level) {
    case EditorConsoleLevel::Warning:
        return zebra ? RGB(34, 30, 17) : RGB(31, 28, 16);
    case EditorConsoleLevel::Error:
        return zebra ? RGB(31, 18, 18) : RGB(28, 16, 16);
    case EditorConsoleLevel::Info:
    default:
        return zebra ? RGB(29, 32, 35) : RGB(26, 28, 31);
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

void DrawPill(HDC dc, RECT rect, EditorConsoleLevel level, std::uint32_t count, bool active) {
    GdiDrawing::DrawSharpFrame(dc, rect, PillBackground(level, active), PillBorder(level, active));

    RECT icon{ rect.left + 6, rect.top, rect.left + 16, rect.bottom };
    DrawText(dc, icon, LevelLabel(level), active ? LevelColor(level) : RGB(122, 130, 144), 12, FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT countRect{ icon.right + 4, rect.top, rect.right - 6, rect.bottom };
    DrawText(dc, countRect, FormatCount(count).c_str(), active ? RGB(200, 205, 211) : RGB(122, 130, 144), 12, FW_NORMAL, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawButton(HDC dc, RECT rect, const EditorTheme& theme, const char* label, bool hovered = false, bool pressed = false) {
    static_cast<void>(theme);
    const COLORREF background = pressed ? RGB(46, 51, 58) : (hovered ? RGB(40, 44, 50) : RGB(34, 37, 42));
    const COLORREF border = pressed ? RGB(92, 104, 118) : (hovered ? RGB(72, 80, 90) : RGB(58, 61, 66));
    const COLORREF text = pressed ? RGB(232, 236, 240) : RGB(200, 205, 211);
    GdiDrawing::DrawSharpFrame(dc, rect, background, border);
    if (pressed) {
        GdiDrawing::FillRectColor(dc, RECT{ rect.left + 1, rect.top + 1, rect.right - 1, rect.top + 2 }, RGB(28, 30, 34));
    }
    RECT textRect = EditorConsoleInsetRect(rect, 8, 0);
    if (pressed) {
        ++textRect.top;
        ++textRect.bottom;
    }
    DrawText(dc, textRect, label, text, 12, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawConsoleToolbar(HDC dc, const EditorConsoleLayoutRects& layout, const EditorTheme& theme, const EditorConsoleState& console) {
    GdiDrawing::FillRectColor(dc, layout.toolbar, RGB(37, 39, 41));
    GdiDrawing::FillRectColor(dc, RECT{ layout.toolbar.left, layout.toolbar.bottom - 1, layout.toolbar.right, layout.toolbar.bottom }, RGB(13, 14, 16));
    DrawPill(dc, layout.infoButton, EditorConsoleLevel::Info, console.Count(EditorConsoleLevel::Info), console.ShowInfo());
    DrawPill(dc, layout.warningButton, EditorConsoleLevel::Warning, console.Count(EditorConsoleLevel::Warning), console.ShowWarnings());
    DrawPill(dc, layout.errorButton, EditorConsoleLevel::Error, console.Count(EditorConsoleLevel::Error), console.ShowErrors());
    DrawButton(dc, layout.copyLineButton, theme, "Copy Line", console.HoveredButton() == EditorConsoleButton::CopyLine, console.PressedButton() == EditorConsoleButton::CopyLine);
    DrawButton(dc, layout.saveLogButton, theme, "Save Log", console.HoveredButton() == EditorConsoleButton::SaveLog, console.PressedButton() == EditorConsoleButton::SaveLog);
    DrawButton(dc, layout.clearButton, theme, "Clear", console.HoveredButton() == EditorConsoleButton::Clear, console.PressedButton() == EditorConsoleButton::Clear);
}

void DrawConsoleRow(HDC dc, RECT row, const EditorTheme& theme, const EditorConsoleEntry& entry, int visibleIndex, bool selected) {
    static_cast<void>(theme);
    const bool zebra = (visibleIndex & 1) != 0;
    GdiDrawing::FillRectColor(dc, row, selected ? RGB(28, 49, 70) : RowBackground(entry.level, zebra));
    if (selected) {
        GdiDrawing::FillRectColor(dc, RECT{ row.left, row.top, row.left + 2, row.bottom }, RGB(79, 129, 184));
    }

    const std::string time = FormatTimestamp(entry.timestampMs);
    DrawText(dc, RECT{ row.left + 10, row.top, row.left + 90, row.bottom }, time.c_str(), RGB(82, 90, 99), 12);

    RECT category{ row.left + 98, row.top + 3, row.left + 222, row.bottom - 3 };
    GdiDrawing::FillRectColor(dc, category, RGB(42, 46, 53));
    DrawText(dc, EditorConsoleInsetRect(category, 6, 0), entry.category.c_str(), RGB(138, 146, 156), 11, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    DrawText(dc, RECT{ row.left + 230, row.top, row.right - 10, row.bottom }, entry.message.c_str(), MessageColor(entry.level), 12);

    GdiDrawing::FillRectColor(dc, RECT{ row.left, row.bottom - 1, row.right, row.bottom }, RGB(30, 32, 35));
}

void DrawConsoleDetailScrollbar(HDC dc, const EditorConsoleLayoutRects& layout, const EditorConsoleState& console) {
    const EditorConsoleDetailScrollMetrics scroll = ResolveEditorConsoleDetailScrollMetrics(layout.detailTextArea, console);
    if (scroll.maxLine <= 0) {
        return;
    }

    GdiDrawing::DrawSharpFrame(dc, layout.detailScrollbarTrack, RGB(22, 24, 27), RGB(38, 42, 47));
    const COLORREF thumbColor = console.IsDetailScrollbarDragging() ? RGB(104, 116, 130) : RGB(76, 86, 98);
    const COLORREF thumbBorder = console.IsDetailScrollbarDragging() ? RGB(128, 142, 158) : RGB(94, 105, 118);
    GdiDrawing::DrawSharpFrame(dc, layout.detailScrollbarThumb, thumbColor, thumbBorder);
    const int midY = layout.detailScrollbarThumb.top + (layout.detailScrollbarThumb.bottom - layout.detailScrollbarThumb.top) / 2;
    GdiDrawing::FillRectColor(dc, RECT{ layout.detailScrollbarThumb.left + 2, midY - 2, layout.detailScrollbarThumb.right - 2, midY - 1 }, RGB(145, 154, 164));
    GdiDrawing::FillRectColor(dc, RECT{ layout.detailScrollbarThumb.left + 2, midY + 1, layout.detailScrollbarThumb.right - 2, midY + 2 }, RGB(145, 154, 164));
}

void DrawConsoleListScrollbar(HDC dc, const EditorConsoleLayoutRects& layout, const EditorConsoleState& console) {
    const EditorConsoleListScrollMetrics scroll = ResolveEditorConsoleListScrollMetrics(layout.listRows, console);
    if (scroll.maxRow <= 0) {
        return;
    }

    GdiDrawing::DrawSharpFrame(dc, layout.listScrollbarTrack, RGB(22, 24, 27), RGB(38, 42, 47));
    const COLORREF thumbColor = console.IsListScrollbarDragging() ? RGB(104, 116, 130) : RGB(76, 86, 98);
    const COLORREF thumbBorder = console.IsListScrollbarDragging() ? RGB(128, 142, 158) : RGB(94, 105, 118);
    GdiDrawing::DrawSharpFrame(dc, layout.listScrollbarThumb, thumbColor, thumbBorder);
}

void DrawConsoleDetail(HDC dc, const EditorConsoleLayoutRects& layout, const EditorTheme& theme, const EditorConsoleState& console) {
    static_cast<void>(theme);
    const EditorConsoleEntry* selected = console.SelectedEntry();
    if (selected == nullptr) {
        return;
    }

    GdiDrawing::FillRectColor(dc, layout.detailSplitter, RGB(26, 28, 31));
    GdiDrawing::FillRectColor(dc, layout.detailSplitterVisual, console.IsDetailResizeDragging() ? RGB(58, 66, 74) : RGB(36, 39, 43));
    GdiDrawing::DrawSharpFrame(dc, layout.detail, RGB(20, 22, 24), RGB(13, 14, 16));
    GdiDrawing::DrawSharpFrame(dc, layout.detailHeader, RGB(26, 28, 31), RGB(30, 32, 35));

    DrawText(dc, RECT{ layout.detailHeader.left + 10, layout.detailHeader.top, layout.detailHeader.left + 88, layout.detailHeader.bottom }, LongLevelLabel(selected->level), LevelColor(selected->level), 12, FW_SEMIBOLD);
    DrawText(dc, RECT{ layout.detailHeader.left + 92, layout.detailHeader.top, layout.detailHeader.left + 220, layout.detailHeader.bottom }, selected->category.c_str(), RGB(106, 122, 138), 11);
    const std::string time = FormatTimestamp(selected->timestampMs);
    DrawText(dc, RECT{ layout.detailHeader.left + 224, layout.detailHeader.top, layout.detailHeader.right - 10, layout.detailHeader.bottom }, time.c_str(), RGB(82, 90, 99), 11);
    DrawConsoleDetailScrollbar(dc, layout, console);
}

void DrawConsolePanel(HDC dc, const RECT& content, const EditorTheme& theme, const EditorConsoleState& console) {
    const EditorConsoleLayoutRects layout = ResolveEditorConsoleLayout(content, console);
    GdiDrawing::FillRectColor(dc, content, RGB(26, 28, 31));
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
        DrawText(dc, layout.listRows, console.Entries().empty() ? "Console is ready. No log entries yet." : "No log entries match the active filters.", RGB(86, 92, 100), 12, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    DrawConsoleListScrollbar(dc, layout, console);
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
        ProjectSettingsPanelRenderer{}.Paint(dc, content, theme, sceneContext);
        break;
    case DockPanelKind::EditorSettings:
        EditorSettingsPanelRenderer{}.Paint(dc, content, theme, sceneContext, renderBackendSettings);
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
