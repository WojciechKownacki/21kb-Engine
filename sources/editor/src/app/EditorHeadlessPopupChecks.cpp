#include "app/EditorHeadlessPopupChecks.hpp"

#if defined(_WIN32)
#include "app/EditorWindowMessageContext.hpp"
#include "app/EditorWindowMessageRouter.hpp"
#include "kb/render/SceneDepthPolicy.hpp"
#include "rendering/EditorOverlayPopupWindow.hpp"
#include "rendering/EditorToolbarLayout.hpp"
#include "rendering/EditorToolbarMenuOverlayWindow.hpp"
#include "rendering/EditorToolbarRenderer.hpp"
#include "rendering/MainWindowBackBufferPainter.hpp"
#include "scene/EditorSceneContext.hpp"

#include <bx/math.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {
namespace {

constexpr wchar_t kEditorWindowClassName[] = L"KBEditorHeadlessEditorWindow";
constexpr wchar_t kViewportWindowClassName[] = L"KBEditorSceneBgfxViewport";
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 760;
constexpr int kOffscreen = -12000;

[[nodiscard]] RECT ToRect(const DockRect& rect) noexcept {
    return RECT{ rect.x, rect.y, rect.x + rect.width, rect.y + rect.height };
}

[[nodiscard]] bool SameRect(const RECT& left, const RECT& right) noexcept {
    return left.left == right.left && left.top == right.top && left.right == right.right && left.bottom == right.bottom;
}

[[nodiscard]] LPARAM PointParam(int x, int y) noexcept {
    return MAKELPARAM(x, y);
}

[[nodiscard]] POINT Center(const RECT& rect) noexcept {
    return POINT{ (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2 };
}

// An editor window as the application builds one, minus the application: its
// messages go through EditorWindowMessageRouter, the router the editor's window
// procedure dispatches to, with the same kinds of state behind it.
class EditorWindowUnderTest final {
public:
    EditorWindowUnderTest(EditorSceneContext& sceneContext, EditorSceneBgfxViewport& viewport) noexcept
        : sceneContext_(sceneContext)
        , viewport_(viewport) {}

    ~EditorWindowUnderTest() {
        Destroy();
    }

    EditorWindowUnderTest(const EditorWindowUnderTest&) = delete;
    EditorWindowUnderTest& operator=(const EditorWindowUnderTest&) = delete;

    [[nodiscard]] bool Create() {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_DBLCLKS;
        windowClass.lpfnWndProc = &EditorWindowUnderTest::WindowProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        windowClass.lpszClassName = kEditorWindowClassName;
        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
        window_ = CreateWindowExW(
            0, kEditorWindowClassName, L"", WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            kOffscreen, kOffscreen, kWindowWidth, kWindowHeight, nullptr, nullptr, instance, this);
        if (window_ == nullptr) {
            return false;
        }
        floatingWindows_.Lifecycle().Configure(instance, window_, metrics_);
        dockController_.Configure(window_, dockModel_, floatingWindows_, metrics_);
        running_ = true;
        routing_ = true;
        // Shown, so it gets painted, but off screen, at the bottom and never activated.
        SetWindowPos(window_, HWND_BOTTOM, kOffscreen, kOffscreen, kWindowWidth, kWindowHeight,
            SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
        return true;
    }

    void Destroy() {
        if (window_ == nullptr) {
            return;
        }
        // The router treats the destruction of its main window as the end of the
        // application, so the window leaves the router before it goes.
        routing_ = false;
        MainWindowBackBufferPainter::HideAllOverlays();
        floatingWindows_.Lifecycle().Shutdown();
        const HWND window = window_;
        window_ = nullptr;
        DestroyWindow(window);
    }

    // Runs everything queued for this thread the way the editor's message loop does.
    void Pump() const {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        MSG message{};
        while (std::chrono::steady_clock::now() < deadline) {
            bool any = false;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
                any = true;
                if (message.message == WM_QUIT) {
                    PostQuitMessage(static_cast<int>(message.wParam));
                    return;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (!any) {
                return;
            }
        }
    }

    void Click(HWND target, POINT point) const {
        PostMessageW(target, WM_MOUSEMOVE, 0, PointParam(point.x, point.y));
        PostMessageW(target, WM_LBUTTONDOWN, MK_LBUTTON, PointParam(point.x, point.y));
        PostMessageW(target, WM_LBUTTONUP, 0, PointParam(point.x, point.y));
        Pump();
    }

    [[nodiscard]] HWND Window() const noexcept {
        return window_;
    }

    [[nodiscard]] EditorShellInteractionState& Shell() noexcept {
        return shell_;
    }

    [[nodiscard]] DockLayout Layout() const {
        RECT client{};
        GetClientRect(window_, &client);
        return dockModel_.Queries().BuildLayout(
            client.right - client.left, client.bottom - client.top,
            metrics_.menuHeight, metrics_.toolbarHeight, metrics_.tabStripHeight,
            metrics_.tabMinWidth, metrics_.tabWidth, metrics_.splitterSize);
    }

    [[nodiscard]] std::optional<DockPanelLayout> ActivePanel(DockPanelKind kind) const {
        for (const DockPanelLayout& panelLayout : Layout().panels) {
            const DockPanel* panel = dockModel_.Queries().FindPanel(panelLayout.panelId);
            if (panelLayout.active && panel != nullptr && panel->kind == kind) {
                return panelLayout;
            }
        }
        return std::nullopt;
    }

private:
    [[nodiscard]] EditorWindowMessageContext Context() noexcept {
        return EditorWindowMessageContext{
            window_, running_, dockModel_, sceneContext_, theme_, metrics_, renderer_, backendSettings_,
            viewport_, floatingWindows_, dockController_, hierarchySelection_, playMode_, shell_, pointerDrag_,
        };
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* editor = reinterpret_cast<EditorWindowUnderTest*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (editor != nullptr && editor->routing_ && editor->window_ == window) {
            return EditorWindowMessageRouter{ editor->Context() }.Handle(window, message, wparam, lparam);
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    EditorSceneContext& sceneContext_;
    EditorSceneBgfxViewport& viewport_;
    HWND window_ = nullptr;
    bool running_ = false;
    bool routing_ = false;
    EditorDockModel dockModel_;
    EditorTheme theme_ = MakeEditorDarkTheme();
    EditorMetrics metrics_{};
    EditorGdiRenderer renderer_;
    EditorRenderBackendSettings backendSettings_;
    EditorFloatingWindowManager floatingWindows_;
    EditorDockController dockController_;
    EditorHierarchySelectionController hierarchySelection_;
    EditorPlayModeState playMode_;
    EditorShellInteractionState shell_;
    EditorPointerDragState pointerDrag_;
};

[[nodiscard]] HWND FindOwnedPopup(HWND owner, const wchar_t* className) noexcept {
    for (HWND window = FindWindowExW(nullptr, nullptr, className, nullptr);
         window != nullptr;
         window = FindWindowExW(nullptr, window, className, nullptr)) {
        if (GetWindow(window, GW_OWNER) == owner) {
            return window;
        }
    }
    return nullptr;
}

[[nodiscard]] HWND FindVisibleViewportSurface(HWND host) noexcept {
    for (HWND child = FindWindowExW(host, nullptr, kViewportWindowClassName, nullptr);
         child != nullptr;
         child = FindWindowExW(host, child, kViewportWindowClassName, nullptr)) {
        if (IsWindowVisible(child) != 0) {
            return child;
        }
    }
    return nullptr;
}

// True when `upper` is above `lower` in the z-order of top-level windows.
[[nodiscard]] bool Above(HWND upper, HWND lower) noexcept {
    for (HWND window = GetWindow(upper, GW_HWNDNEXT); window != nullptr; window = GetWindow(window, GW_HWNDNEXT)) {
        if (window == lower) {
            return true;
        }
    }
    return false;
}

// Presents the scene into the window's scene panel, which is what makes the
// panel's child render surface visible there.
[[nodiscard]] bool PresentScenePanel(
    EditorSceneBgfxViewport& viewport, EditorSceneContext& sceneContext, HWND host, const DockPanelLayout& panel) {
    const RECT bounds = ToRect(panel.content);
    const auto width = static_cast<std::uint32_t>(bounds.right - bounds.left);
    const auto height = static_cast<std::uint32_t>(bounds.bottom - bounds.top);
    EditorSceneBgfxViewport::PresentSettings settings{};
    settings.renderWidth = width;
    settings.renderHeight = height;
    settings.viewportKey = panel.panelId;
    kb::render::SceneRenderCamera camera{};
    bx::mtxLookAt(camera.view.data(), bx::Vec3{ 0.0F, 0.0F, 3.0F }, bx::Vec3{ 0.0F, 0.0F, 0.0F });
    kb::render::SceneDepthPolicy::MakePerspective(
        camera.projection.data(), 60.0F, static_cast<float>(width) / static_cast<float>(height), 0.05F, 100.0F,
        kb::render::SceneDepthPolicy::HomogeneousDepth());
    settings.cameraOverride = camera;
    settings.sceneRevision = sceneContext.SceneRenderRevision();
    settings.sceneDirtyBaseRevision = settings.sceneRevision;
    settings.sceneFullSyncRequired = true;
    settings.editorSceneOverlaysEnabled = false;
    settings.selectionMaskEnabled = false;
    settings.selectionOutlineEnabled = false;
    settings.drawSafeArea = false;
    viewport.BeginPaintLayout(host);
    viewport.Present(host, bounds, sceneContext.Scene(), settings);
    viewport.EndPaintLayout();
    return std::string_view{ viewport.ActiveBackendLabel() } != "Not initialized";
}

// What the window paints, read back through WM_PRINTCLIENT (32-bit BGRA rows).
struct ClientPixels {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;

    [[nodiscard]] int BrightPixelsIn(const RECT& rect, int threshold) const noexcept {
        int count = 0;
        for (int y = std::max(0, static_cast<int>(rect.top)); y < std::min(height, static_cast<int>(rect.bottom)); ++y) {
            for (int x = std::max(0, static_cast<int>(rect.left)); x < std::min(width, static_cast<int>(rect.right)); ++x) {
                const std::uint32_t pixel = pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
                const int blue = static_cast<int>(pixel & 0xFFU);
                const int green = static_cast<int>((pixel >> 8U) & 0xFFU);
                const int red = static_cast<int>((pixel >> 16U) & 0xFFU);
                if (std::max(red, std::max(green, blue)) >= threshold) {
                    ++count;
                }
            }
        }
        return count;
    }
};

[[nodiscard]] ClientPixels CaptureClient(HWND window) {
    ClientPixels capture{};
    RECT client{};
    GetClientRect(window, &client);
    capture.width = client.right - client.left;
    capture.height = client.bottom - client.top;
    if (capture.width <= 0 || capture.height <= 0) {
        return {};
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = capture.width;
    info.bmiHeader.biHeight = -capture.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC memory = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (memory == nullptr || bitmap == nullptr || bits == nullptr) {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (memory != nullptr) {
            DeleteDC(memory);
        }
        return {};
    }
    const HGDIOBJ previous = SelectObject(memory, bitmap);
    SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT);
    GdiFlush();
    const auto* source = static_cast<const std::uint32_t*>(bits);
    capture.pixels.assign(source, source + static_cast<std::size_t>(capture.width) * static_cast<std::size_t>(capture.height));
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    return capture;
}

// Checks the open menu's popup: a visible top-level window owned by the editor
// window, above it, exactly over the dropdown, over the scene viewport, with every
// row painted. Appends what failed to `detail`.
[[nodiscard]] bool CheckMenuPopup(
    EditorWindowUnderTest& editor, HWND viewportSurface, EditorMenuCommand menu, std::string_view name, std::string& detail) {
    const HWND host = editor.Window();
    const HWND popup = FindOwnedPopup(host, EditorToolbarMenuOverlayWindow::ClassName);
    if (editor.Shell().OpenMenu() != menu) {
        detail += std::string{ name } + "-not-open;";
        return false;
    }
    if (popup == nullptr || IsWindowVisible(popup) == 0) {
        detail += std::string{ name } + "-no-popup;";
        return false;
    }
    bool succeeded = true;
    if (GetAncestor(popup, GA_PARENT) != GetDesktopWindow()) {
        detail += std::string{ name } + "-popup-not-top-level;";
        succeeded = false;
    }
    if (!Above(popup, host)) {
        detail += std::string{ name } + "-popup-below-window;";
        succeeded = false;
    }

    const int rows = editor.Shell().MenuRowCount(menu);
    const EditorMenuRects rects = EditorToolbarRenderer::ResolveMenu(ToRect(editor.Layout().menu), menu, rows);
    const RECT paintBounds = EditorToolbarRenderer::MenuDropdownPaintBounds(rects);
    RECT popupRect{};
    GetWindowRect(popup, &popupRect);
    if (rows <= 0 || !SameRect(popupRect, EditorOverlayPopupWindow::OwnerClientToScreen(host, paintBounds))) {
        detail += std::string{ name } + "-popup-misplaced;";
        succeeded = false;
    }
    RECT surfaceRect{};
    RECT overlap{};
    GetWindowRect(viewportSurface, &surfaceRect);
    if (IntersectRect(&overlap, &surfaceRect, &popupRect) == 0) {
        detail += std::string{ name } + "-not-over-viewport;";
        succeeded = false;
    }

    const ClientPixels pixels = CaptureClient(popup);
    for (int row = 0; row < rows; ++row) {
        RECT rowRect = rects.dropdownRows[static_cast<std::size_t>(row)];
        OffsetRect(&rowRect, -paintBounds.left, -paintBounds.top);
        // A row's label is light text on the dark menu surface.
        if (pixels.BrightPixelsIn(rowRect, 150) < 8) {
            detail += std::string{ name } + "-row-" + std::to_string(row) + "-not-painted;";
            succeeded = false;
        }
    }
    return succeeded;
}

[[nodiscard]] POINT MenuItemCenter(EditorWindowUnderTest& editor, EditorMenuCommand menu) {
    const EditorMenuRects rects = EditorToolbarRenderer::ResolveMenu(ToRect(editor.Layout().menu), EditorMenuCommand::None, 0);
    return Center(EditorToolbarLayout::MenuRectByCommand(rects, menu));
}

} // namespace

EditorHeadlessPopupCheckResult EditorHeadlessPopupChecks::VerifyToolbarMenuOverlay(
    EditorSceneContext& sceneContext, EditorSceneBgfxViewport& viewport) {
    EditorHeadlessPopupCheckResult result{};
    EditorWindowUnderTest editor(sceneContext, viewport);
    if (!editor.Create()) {
        result.detail = "editor-window-not-created";
        return result;
    }
    const std::optional<DockPanelLayout> scenePanel = editor.ActivePanel(DockPanelKind::Scene);
    if (!scenePanel.has_value() || !PresentScenePanel(viewport, sceneContext, editor.Window(), *scenePanel)) {
        result.detail = "scene-panel-not-presented";
        return result;
    }
    editor.Pump();
    const HWND viewportSurface = FindVisibleViewportSurface(editor.Window());
    if (viewportSurface == nullptr) {
        result.detail = "no-viewport-surface";
        return result;
    }

    std::string& detail = result.detail;
    bool succeeded = true;

    // World: the longest fixed menu, opened with a click on the menu bar.
    editor.Click(editor.Window(), MenuItemCenter(editor, EditorMenuCommand::World));
    succeeded = CheckMenuPopup(editor, viewportSurface, EditorMenuCommand::World, "world", detail) && succeeded;
    if (const HWND popup = FindOwnedPopup(editor.Window(), EditorToolbarMenuOverlayWindow::ClassName); popup != nullptr) {
        const EditorMenuRects rects = EditorToolbarRenderer::ResolveMenu(
            ToRect(editor.Layout().menu), EditorMenuCommand::World, editor.Shell().MenuRowCount(EditorMenuCommand::World));
        const RECT paintBounds = EditorToolbarRenderer::MenuDropdownPaintBounds(rects);
        const auto rowCenter = [&](int row) {
            POINT point = Center(rects.dropdownRows[static_cast<std::size_t>(row)]);
            point.x -= paintBounds.left;
            point.y -= paintBounds.top;
            return point;
        };
        // Hover over the popup reaches the toolbar's hover tracking.
        const POINT hover = rowCenter(2);
        PostMessageW(popup, WM_MOUSEMOVE, 0, PointParam(hover.x, hover.y));
        editor.Pump();
        if (editor.Shell().HoveredMenuRow() != std::optional<int>{ 2 }) {
            detail += "world-hover-not-forwarded;";
            succeeded = false;
        }
        // A click on "Show or Hide Cell Grid" runs that row and closes the menu.
        const bool gridVisible = sceneContext.WorldPartition().GridVisible();
        editor.Click(popup, rowCenter(4));
        if (sceneContext.WorldPartition().GridVisible() == gridVisible) {
            detail += "world-row-click-not-forwarded;";
            succeeded = false;
        }
        sceneContext.WorldPartition().SetGridVisible(gridVisible);
        if (editor.Shell().OpenMenu() != EditorMenuCommand::None || IsWindowVisible(popup) != 0) {
            detail += "world-not-closed-by-row;";
            succeeded = false;
        }
    }

    // Layout: as long as the project's layouts make it.
    editor.Click(editor.Window(), MenuItemCenter(editor, EditorMenuCommand::Layout));
    succeeded = CheckMenuPopup(editor, viewportSurface, EditorMenuCommand::Layout, "layout", detail) && succeeded;
    editor.Click(editor.Window(), MenuItemCenter(editor, EditorMenuCommand::Layout));
    if (const HWND popup = FindOwnedPopup(editor.Window(), EditorToolbarMenuOverlayWindow::ClassName);
        editor.Shell().OpenMenu() != EditorMenuCommand::None || (popup != nullptr && IsWindowVisible(popup) != 0)) {
        detail += "layout-not-closed;";
        succeeded = false;
    }

    result.succeeded = succeeded;
    if (succeeded) {
        detail = "world,layout";
    }
    return result;
}

} // namespace kb::editor

#endif
