#include "EditorGdiRenderer.hpp"

#if defined(_WIN32)
#include <algorithm>

namespace kb::editor {
namespace {

[[nodiscard]] constexpr COLORREF ToColorRef(EditorColor color) {
    return RGB(color.r, color.g, color.b);
}

struct ScopedBrush {
    explicit ScopedBrush(COLORREF color)
        : handle(CreateSolidBrush(color)) {
    }

    ~ScopedBrush() {
        if (handle != nullptr) {
            DeleteObject(handle);
        }
    }

    ScopedBrush(const ScopedBrush&) = delete;
    ScopedBrush& operator=(const ScopedBrush&) = delete;

    HBRUSH handle = nullptr;
};

struct ScopedFont {
    ScopedFont(int pointSize, int weight)
        : handle(CreateFontW(
              -pointSize,
              0,
              0,
              0,
              weight,
              FALSE,
              FALSE,
              FALSE,
              DEFAULT_CHARSET,
              OUT_DEFAULT_PRECIS,
              CLIP_DEFAULT_PRECIS,
              CLEARTYPE_QUALITY,
              DEFAULT_PITCH | FF_DONTCARE,
              L"Segoe UI")) {
    }

    ~ScopedFont() {
        if (handle != nullptr) {
            DeleteObject(handle);
        }
    }

    ScopedFont(const ScopedFont&) = delete;
    ScopedFont& operator=(const ScopedFont&) = delete;

    HFONT handle = nullptr;
};

struct ScopedPen {
    ScopedPen(int width, COLORREF color)
        : handle(CreatePen(PS_SOLID, width, color)) {
    }

    ~ScopedPen() {
        if (handle != nullptr) {
            DeleteObject(handle);
        }
    }

    ScopedPen(const ScopedPen&) = delete;
    ScopedPen& operator=(const ScopedPen&) = delete;

    HPEN handle = nullptr;
};

RECT Inset(RECT rect, int amount) {
    rect.left += amount;
    rect.top += amount;
    rect.right -= amount;
    rect.bottom -= amount;
    return rect;
}

void FillRectColor(HDC dc, const RECT& rect, COLORREF color) {
    ScopedBrush brush(color);
    FillRect(dc, &rect, brush.handle);
}

void DrawTextBlock(HDC dc, RECT rect, const char* text, COLORREF color) {
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
}

void DrawSharpFrame(HDC dc, const RECT& rect, COLORREF fill, COLORREF border) {
    FillRectColor(dc, rect, fill);

    ScopedPen borderPen(1, border);
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, borderPen.handle));
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
}

void DrawPanel(HDC dc, const RECT& rect, const DockPanel& panel, const EditorTheme& theme, const EditorMetrics& metrics, COLORREF fill, bool active) {
    DrawSharpFrame(dc, rect, fill, ToColorRef(theme.borderPanel));

    RECT tabStrip{ rect.left + 1, rect.top + 1, rect.right - 1, rect.top + metrics.tabStripHeight };
    FillRectColor(dc, tabStrip, ToColorRef(theme.strip));

    RECT tab{ tabStrip.left, tabStrip.top, std::min(tabStrip.left + metrics.tabWidth, tabStrip.right), tabStrip.bottom };
    DrawSharpFrame(
        dc,
        tab,
        active ? ToColorRef(theme.tabActive) : ToColorRef(theme.tabInactive),
        active ? ToColorRef(theme.borderPanel) : ToColorRef(theme.strip));

    RECT titleRect{ tab.left + 12, tab.top + 6, tab.right - 12, tab.bottom - 4 };
    DrawTextBlock(dc, titleRect, panel.title.c_str(), ToColorRef(theme.textPrimary));
}

void DrawToolbarButton(HDC dc, const RECT& rect, const char* label, const EditorTheme& theme, bool active) {
    DrawSharpFrame(
        dc,
        rect,
        active ? ToColorRef(theme.tabActive) : ToColorRef(theme.toolbarButton),
        active ? ToColorRef(theme.accent) : ToColorRef(theme.borderChrome));
    DrawTextBlock(
        dc,
        { rect.left + 12, rect.top + 7, rect.right - 12, rect.bottom - 4 },
        label,
        active ? ToColorRef(theme.textPrimary) : ToColorRef(theme.textSecondary));
}

void DrawSceneGrid(HDC dc, RECT scene, const EditorTheme& theme, const EditorMetrics& metrics) {
    RECT sceneInner = Inset(scene, 20);
    sceneInner.top += metrics.tabStripHeight + 12;

    ScopedPen gridPen(1, ToColorRef(theme.gridLine));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, gridPen.handle));

    for (int x = sceneInner.left; x < sceneInner.right; x += 32) {
        MoveToEx(dc, x, sceneInner.top, nullptr);
        LineTo(dc, x, sceneInner.bottom);
    }

    for (int y = sceneInner.top; y < sceneInner.bottom; y += 32) {
        MoveToEx(dc, sceneInner.left, y, nullptr);
        LineTo(dc, sceneInner.right, y);
    }

    ScopedPen accentPen(2, ToColorRef(theme.accent));
    SelectObject(dc, accentPen.handle);

    const int centerX = (sceneInner.left + sceneInner.right) / 2;
    const int centerY = (sceneInner.top + sceneInner.bottom) / 2;
    Ellipse(dc, centerX - 48, centerY - 48, centerX + 48, centerY + 48);

    SelectObject(dc, oldPen);
}

DockPanel FindPanel(const EditorDockModel& dockModel, const char* title) {
    for (const auto& panel : dockModel.Panels()) {
        if (panel.title == title) {
            return panel;
        }
    }

    return DockPanel{ .title = title };
}

} // namespace

void EditorGdiRenderer::Paint(HWND window, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics) const {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);

    RECT client{};
    GetClientRect(window, &client);

    FillRectColor(dc, client, ToColorRef(theme.background));
    SetBkMode(dc, TRANSPARENT);

    ScopedFont titleFont(16, FW_SEMIBOLD);
    ScopedFont bodyFont(14, FW_NORMAL);
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, bodyFont.handle));

    RECT menu{ client.left, client.top, client.right, client.top + metrics.menuHeight };
    FillRectColor(dc, menu, ToColorRef(theme.menuBar));
    DrawTextBlock(dc, { 20, 10, 220, 34 }, "21kb Engine", ToColorRef(theme.textPrimary));
    DrawTextBlock(dc, { 140, 10, 420, 34 }, "Native editor shell", ToColorRef(theme.textSecondary));

    RECT toolbar{ client.left, menu.bottom, client.right, menu.bottom + metrics.toolbarHeight };
    FillRectColor(dc, toolbar, ToColorRef(theme.toolbar));
    DrawToolbarButton(dc, { 16, toolbar.top + 9, 92, toolbar.bottom - 9 }, "Select", theme, true);
    DrawToolbarButton(dc, { 100, toolbar.top + 9, 176, toolbar.bottom - 9 }, "Move", theme, false);
    DrawToolbarButton(dc, { 184, toolbar.top + 9, 268, toolbar.bottom - 9 }, "Rotate", theme, false);
    DrawToolbarButton(dc, { 276, toolbar.top + 9, 352, toolbar.bottom - 9 }, "Scale", theme, false);

    const int width = client.right - client.left;
    const int height = client.bottom - toolbar.bottom;
    const int leftWidth = std::max(260, width / 6);
    const int rightWidth = std::max(320, width / 5);
    const int bottomHeight = std::max(220, height / 4);
    const int splitter = metrics.splitterSize;
    const int top = toolbar.bottom + splitter;

    RECT hierarchy{ 0, top, leftWidth, client.bottom - bottomHeight - splitter };
    RECT scene{ hierarchy.right + splitter, top, client.right - rightWidth - splitter, hierarchy.bottom };
    RECT inspector{ scene.right + splitter, top, client.right, scene.bottom };
    RECT bottomSplitter{ 0, hierarchy.bottom, client.right, hierarchy.bottom + splitter };
    RECT assets{ 0, bottomSplitter.bottom, width / 2 - splitter / 2, client.bottom };
    RECT console{ assets.right + splitter, bottomSplitter.bottom, client.right, client.bottom };

    FillRectColor(dc, { hierarchy.right, top, hierarchy.right + splitter, hierarchy.bottom }, ToColorRef(theme.splitter));
    FillRectColor(dc, { scene.right, top, scene.right + splitter, scene.bottom }, ToColorRef(theme.splitter));
    FillRectColor(dc, bottomSplitter, ToColorRef(theme.splitter));
    FillRectColor(dc, { assets.right, assets.top, assets.right + splitter, assets.bottom }, ToColorRef(theme.splitter));

    SelectObject(dc, titleFont.handle);
    DrawPanel(dc, hierarchy, FindPanel(dockModel, "Hierarchy"), theme, metrics, ToColorRef(theme.panel), true);
    DrawPanel(dc, scene, FindPanel(dockModel, "Scene"), theme, metrics, ToColorRef(theme.chrome), true);
    DrawPanel(dc, inspector, FindPanel(dockModel, "Inspector"), theme, metrics, ToColorRef(theme.panel), true);
    DrawPanel(dc, assets, FindPanel(dockModel, "Assets"), theme, metrics, ToColorRef(theme.panel), true);
    DrawPanel(dc, console, FindPanel(dockModel, "Console"), theme, metrics, ToColorRef(theme.panel), true);

    SelectObject(dc, bodyFont.handle);
    RECT hierarchyContent = Inset(hierarchy, metrics.panelPadding);
    RECT inspectorContent = Inset(inspector, metrics.panelPadding);
    RECT assetsContent = Inset(assets, metrics.panelPadding);
    RECT consoleContent = Inset(console, metrics.panelPadding);
    hierarchyContent.top += metrics.tabStripHeight;
    inspectorContent.top += metrics.tabStripHeight;
    assetsContent.top += metrics.tabStripHeight;
    consoleContent.top += metrics.tabStripHeight;

    DrawTextBlock(dc, hierarchyContent, "Camera\nDirectional Light\nPlayer\nCanvas Root", ToColorRef(theme.textSecondary));
    DrawTextBlock(dc, inspectorContent, "Entity: Camera\nTransform\nCamera\nRender Layer", ToColorRef(theme.textSecondary));
    DrawTextBlock(dc, assetsContent, "Scenes\nMaterials\nMeshes\nTextures", ToColorRef(theme.textSecondary));
    DrawTextBlock(dc, consoleContent, "[info] Native window initialized\n[todo] Custom renderer-backed UI", ToColorRef(theme.textDisabled));

    DrawSceneGrid(dc, scene, theme, metrics);

    SelectObject(dc, oldFont);
    EndPaint(window, &paint);
}

} // namespace kb::editor

#endif
