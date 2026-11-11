#pragma once

#if defined(_WIN32)
#include "app/EditorWindowMessageContext.hpp"
#include "app/EditorWindowMessageRouter.hpp"
#include "rendering/MainWindowBackBufferPainter.hpp"
#include "scene/EditorSceneContext.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

// The off-screen editor window the headless checks drive with real window messages,
// and the read-back of what a window paints.
namespace kb::editor::headless {

inline constexpr wchar_t kEditorWindowClassName[] = L"KBEditorHeadlessEditorWindow";
inline constexpr int kWindowWidth = 1280;
inline constexpr int kWindowHeight = 760;
inline constexpr int kOffscreen = -12000;

[[nodiscard]] inline RECT ToRect(const DockRect& rect) noexcept {
    return RECT{ rect.x, rect.y, rect.x + rect.width, rect.y + rect.height };
}

[[nodiscard]] inline LPARAM PointParam(int x, int y) noexcept {
    return MAKELPARAM(x, y);
}

[[nodiscard]] inline POINT Center(const RECT& rect) noexcept {
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
        // Panels torn off this window get the same procedure, as the editor's own floating windows do.
        windowClass.lpszClassName = EditorFloatingWindowManager::WindowClassName;
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
        // Drain native framebuffer creation before destroying this test host;
        // the render thread must not create a swap chain for an expired HWND.
        viewport_.SetHostSurfaceSuspended(window_, true);
        for (int frame = 0; frame < 3; ++frame) static_cast<void>(viewport_.AdvanceAsyncReadbacks());
        const HWND window = window_;
        window_ = nullptr;
        DestroyWindow(window);
        for (int frame = 0; frame < 3; ++frame) static_cast<void>(viewport_.AdvanceAsyncReadbacks());
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

    [[nodiscard]] EditorPlayMode Mode() const noexcept { return playMode_.Mode(); }
    [[nodiscard]] bool GameHasInput() const noexcept { return playMode_.GameHasInput(); }
    [[nodiscard]] EditorDockModel& Dock() noexcept { return dockModel_; }
    [[nodiscard]] EditorFloatingWindowManager& FloatingWindows() noexcept { return floatingWindows_; }
    [[nodiscard]] const EditorDockController& DockController() const noexcept { return dockController_; }
    [[nodiscard]] const EditorMetrics& Metrics() const noexcept { return metrics_; }

    [[nodiscard]] EditorShellInteractionState& Shell() noexcept {
        return shell_;
    }

    [[nodiscard]] DockLayout Layout() const {
        RECT client{};
        GetClientRect(window_, &client);
        return dockModel_.Queries().BuildLayout(
            client.right - client.left, client.bottom - client.top,
            metrics_.menuHeight, metrics_.statusBarHeight, metrics_.tabStripHeight,
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
        if (editor != nullptr && editor->routing_ &&
            (editor->window_ == window || editor->floatingWindows_.Queries().IsFloatingWindow(window))) {
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

// What the window paints, read back through WM_PRINTCLIENT (32-bit BGRA rows).
struct ClientPixels {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;

    [[nodiscard]] bool Save(const std::filesystem::path& path) const {
        if (pixels.empty()) return false;
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        BITMAPFILEHEADER file{}; file.bfType = 0x4d42;
        file.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size() * sizeof(std::uint32_t));
        BITMAPINFOHEADER info{}; info.biSize = sizeof(info); info.biWidth = width; info.biHeight = -height;
        info.biPlanes = 1; info.biBitCount = 32; info.biCompression = BI_RGB;
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&file), sizeof(file));
        output.write(reinterpret_cast<const char*>(&info), sizeof(info));
        output.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size() * sizeof(std::uint32_t)));
        return static_cast<bool>(output);
    }

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

[[nodiscard]] inline ClientPixels CaptureClient(HWND window) {
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

} // namespace kb::editor::headless
#endif
