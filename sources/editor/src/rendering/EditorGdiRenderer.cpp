#include "rendering/EditorGdiRenderer.hpp"

#if defined(_WIN32)
#include "rendering/FloatingWindowControlRenderer.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/PanelContentRenderer.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

void DrawPanelChrome(HDC dc, const RECT& rect, const DockPanel& panel, const EditorTheme& theme, const EditorMetrics& metrics, COLORREF fill, bool active) {
    GdiDrawing::DrawSharpFrame(dc, rect, fill, GdiDrawing::ToColorRef(theme.borderPanel));

    RECT tabStrip{ rect.left + 1, rect.top + 1, rect.right - 1, rect.top + metrics.tabStripHeight };
    GdiDrawing::FillRectColor(dc, tabStrip, GdiDrawing::ToColorRef(theme.strip));

    RECT tab{ tabStrip.left, tabStrip.top, std::min(tabStrip.left + metrics.tabWidth, tabStrip.right), tabStrip.bottom };
    GdiDrawing::DrawSharpFrame(
        dc,
        tab,
        active ? GdiDrawing::ToColorRef(theme.tabActive) : GdiDrawing::ToColorRef(theme.tabInactive),
        active ? GdiDrawing::ToColorRef(theme.borderPanel) : GdiDrawing::ToColorRef(theme.strip));

    RECT titleRect{ tab.left + 8, tab.top, tab.right - 8, tab.bottom };
    GdiDrawing::DrawTabText(dc, titleRect, panel.title.c_str(), GdiDrawing::ToColorRef(theme.textPrimary));
}

void DrawToolbarButton(HDC dc, const RECT& rect, const char* label, const EditorTheme& theme, bool active) {
    GdiDrawing::DrawSharpFrame(
        dc,
        rect,
        active ? GdiDrawing::ToColorRef(theme.tabActive) : GdiDrawing::ToColorRef(theme.toolbarButton),
        active ? GdiDrawing::ToColorRef(theme.accent) : GdiDrawing::ToColorRef(theme.borderChrome));
    GdiDrawing::DrawTextBlock(
        dc,
        { rect.left + 12, rect.top + 7, rect.right - 12, rect.bottom - 4 },
        label,
        active ? GdiDrawing::ToColorRef(theme.textPrimary) : GdiDrawing::ToColorRef(theme.textSecondary));
}

void DrawDropPreview(HDC dc, const DockDropPreview& preview, const EditorTheme& theme) {
    if (preview.rect.Empty()) {
        return;
    }

    RECT previewRect = GdiDrawing::ToRect(preview.rect);
    if (preview.kind == DockDropPreviewKind::StripMarker) {
        GdiDrawing::FillRectColor(dc, previewRect, GdiDrawing::ToColorRef(theme.accent));
        return;
    }

    GdiDrawing::FillRectAlpha(dc, previewRect, RGB(64, 102, 146), 112);
    ScopedPen pen(2, GdiDrawing::ToColorRef(theme.accent));
    HPEN oldPenPreview = static_cast<HPEN>(SelectObject(dc, pen.handle));
    HBRUSH oldBrushPreview = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    Rectangle(dc, previewRect.left + 1, previewRect.top + 1, previewRect.right - 1, previewRect.bottom - 1);
    SelectObject(dc, oldBrushPreview);
    SelectObject(dc, oldPenPreview);
}

} // namespace

void EditorGdiRenderer::Paint(HWND window, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics, const DockDropPreview* preview) const {
    PAINTSTRUCT paint{};
    HDC targetDc = BeginPaint(window, &paint);

    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    ScopedCompatibleDc memoryDc(targetDc);
    ScopedBitmap backBuffer(targetDc, width, height);
    HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memoryDc.handle, backBuffer.handle));
    HDC dc = memoryDc.handle;

    GdiDrawing::FillRectColor(dc, client, GdiDrawing::ToColorRef(theme.background));
    SetBkMode(dc, TRANSPARENT);

    ScopedFont titleFont(16, FW_SEMIBOLD);
    ScopedFont bodyFont(14, FW_NORMAL);
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, bodyFont.handle));

    const DockLayout layout = dockModel.BuildLayout(
        width,
        height,
        metrics.menuHeight,
        metrics.toolbarHeight,
        metrics.tabStripHeight,
        metrics.tabMinWidth,
        metrics.tabWidth,
        metrics.splitterSize,
        metrics.panelPadding);

    RECT menu = GdiDrawing::ToRect(layout.menu);
    GdiDrawing::FillRectColor(dc, menu, GdiDrawing::ToColorRef(theme.menuBar));
    GdiDrawing::DrawTextBlock(dc, { 20, 10, 220, 34 }, "21kb Engine", GdiDrawing::ToColorRef(theme.textPrimary));
    GdiDrawing::DrawTextBlock(dc, { 140, 10, 420, 34 }, "Docking workspace", GdiDrawing::ToColorRef(theme.textSecondary));

    RECT toolbar = GdiDrawing::ToRect(layout.toolbar);
    GdiDrawing::FillRectColor(dc, toolbar, GdiDrawing::ToColorRef(theme.toolbar));
    DrawToolbarButton(dc, { 16, toolbar.top + 9, 92, toolbar.bottom - 9 }, "Select", theme, true);
    DrawToolbarButton(dc, { 100, toolbar.top + 9, 176, toolbar.bottom - 9 }, "Move", theme, false);
    DrawToolbarButton(dc, { 184, toolbar.top + 9, 268, toolbar.bottom - 9 }, "Rotate", theme, false);
    DrawToolbarButton(dc, { 276, toolbar.top + 9, 352, toolbar.bottom - 9 }, "Scale", theme, false);

    for (const DockSplitterLayout& splitter : layout.splitters) {
        GdiDrawing::FillRectColor(dc, GdiDrawing::ToRect(splitter.rect), GdiDrawing::ToColorRef(theme.splitter));
    }

    SelectObject(dc, titleFont.handle);
    for (const DockLeafLayout& leaf : layout.leaves) {
        const DockPanel* activePanel = dockModel.FindPanel(leaf.activePanelId);
        if (activePanel == nullptr) {
            continue;
        }

        const bool scene = activePanel->kind == DockPanelKind::Scene;
        GdiDrawing::DrawSharpFrame(dc, GdiDrawing::ToRect(leaf.frame), scene ? GdiDrawing::ToColorRef(theme.chrome) : GdiDrawing::ToColorRef(theme.panel), GdiDrawing::ToColorRef(theme.borderPanel));
        GdiDrawing::FillRectColor(dc, GdiDrawing::ToRect(leaf.tabStrip), GdiDrawing::ToColorRef(theme.strip));
    }

    for (const DockPanelLayout& panelLayout : layout.panels) {
        const DockPanel* panel = dockModel.FindPanel(panelLayout.panelId);
        if (panel == nullptr) {
            continue;
        }

        GdiDrawing::DrawSharpFrame(
            dc,
            GdiDrawing::ToRect(panelLayout.tab),
            panelLayout.active ? GdiDrawing::ToColorRef(theme.tabActive) : GdiDrawing::ToColorRef(theme.tabInactive),
            panelLayout.active ? GdiDrawing::ToColorRef(theme.borderPanel) : GdiDrawing::ToColorRef(theme.strip));
        RECT titleRect = GdiDrawing::ToRect(panelLayout.tab);
        titleRect.left += 8;
        titleRect.right -= 8;
        GdiDrawing::DrawTabText(dc, titleRect, panel->title.c_str(), GdiDrawing::ToColorRef(panelLayout.active ? theme.textPrimary : theme.textSecondary));
    }

    PanelContentRenderer contentRenderer;
    SelectObject(dc, bodyFont.handle);
    for (const DockPanelLayout& panelLayout : layout.panels) {
        const DockPanel* panel = dockModel.FindPanel(panelLayout.panelId);
        if (panel == nullptr || !panelLayout.active) {
            continue;
        }

        const RECT content = GdiDrawing::ToRect(panelLayout.content);
        contentRenderer.Paint(dc, content, content, *panel, theme, metrics, false);
    }

    if (preview != nullptr) {
        DrawDropPreview(dc, *preview, theme);
    }

    SelectObject(dc, oldFont);
    BitBlt(targetDc, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(memoryDc.handle, oldBitmap);
    EndPaint(window, &paint);
}

void EditorGdiRenderer::PaintFloating(HWND window, const DockPanel& panel, const EditorTheme& theme, const EditorMetrics& metrics) const {
    PAINTSTRUCT paint{};
    HDC targetDc = BeginPaint(window, &paint);

    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    ScopedCompatibleDc memoryDc(targetDc);
    ScopedBitmap backBuffer(targetDc, width, height);
    HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memoryDc.handle, backBuffer.handle));
    HDC dc = memoryDc.handle;

    GdiDrawing::FillRectColor(dc, client, GdiDrawing::ToColorRef(theme.background));
    SetBkMode(dc, TRANSPARENT);

    ScopedFont titleFont(16, FW_SEMIBOLD);
    ScopedFont bodyFont(14, FW_NORMAL);
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, titleFont.handle));

    GdiDrawing::DrawSharpFrame(dc, client, GdiDrawing::ToColorRef(theme.background), GdiDrawing::ToColorRef(theme.borderChrome));

    RECT panelRect = GdiDrawing::Inset(client, 1);
    DrawPanelChrome(dc, panelRect, panel, theme, metrics, GdiDrawing::ToColorRef(panel.kind == DockPanelKind::Scene ? theme.chrome : theme.panel), true);

    FloatingWindowControlRenderer{}.Paint(dc, client, theme, metrics);

    SelectObject(dc, bodyFont.handle);
    RECT content = GdiDrawing::Inset(panelRect, metrics.panelPadding);
    content.top += metrics.tabStripHeight;
    PanelContentRenderer{}.Paint(dc, content, panelRect, panel, theme, metrics, true);

    SelectObject(dc, oldFont);
    BitBlt(targetDc, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(memoryDc.handle, oldBitmap);
    EndPaint(window, &paint);
}

} // namespace kb::editor

#endif
