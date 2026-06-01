#include "kb/render/DisplayConfig.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/SimpleTriangle.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr wchar_t kWindowClassName[] = L"KBRenderSmokeWindow";
constexpr wchar_t kWindowTitle[] = L"21kb bgfx smoke";
constexpr int kInitialWidth = 1280;
constexpr int kInitialHeight = 720;

class Win32RenderSurface final : public kb::render::RenderSurface {
public:
    explicit Win32RenderSurface(HWND window) noexcept
        : window_(window) {}

    [[nodiscard]] std::uint32_t Width() const noexcept override {
        RECT rect{};
        if (GetClientRect(window_, &rect) == 0) {
            return 0;
        }
        return static_cast<std::uint32_t>(std::max<LONG>(0, rect.right - rect.left));
    }

    [[nodiscard]] std::uint32_t Height() const noexcept override {
        RECT rect{};
        if (GetClientRect(window_, &rect) == 0) {
            return 0;
        }
        return static_cast<std::uint32_t>(std::max<LONG>(0, rect.bottom - rect.top));
    }

    [[nodiscard]] void* NativeWindowHandle() const noexcept override {
        return window_;
    }

    [[nodiscard]] void* NativeDisplayHandle() const noexcept override {
        return nullptr;
    }

private:
    HWND window_ = nullptr;
};

struct SmokeWindowState {
    kb::render::Renderer* renderer = nullptr;
};

LRESULT CALLBACK SmokeWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<SmokeWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
    case WM_NCCREATE: {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_SIZE:
        if (state != nullptr && state->renderer != nullptr && wParam != SIZE_MINIMIZED) {
            const auto width = static_cast<std::uint32_t>(LOWORD(lParam));
            const auto height = static_cast<std::uint32_t>(HIWORD(lParam));
            state->renderer->OnResize(width, height);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

[[nodiscard]] HWND CreateSmokeWindow(HINSTANCE instance, SmokeWindowState& state) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &SmokeWindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        return nullptr;
    }

    RECT rect{0, 0, kInitialWidth, kInitialHeight};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    return CreateWindowExW(
        0,
        kWindowClassName,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        instance,
        &state);
}

[[nodiscard]] std::uint32_t ParseMaxFrames(int argc, char** argv) {
    constexpr std::uint32_t kDefaultFrames = 600;
    constexpr char kPrefix[] = "--max-frames=";
    constexpr std::size_t kPrefixLength = sizeof(kPrefix) - 1;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (arg == nullptr || strncmp(arg, kPrefix, kPrefixLength) != 0) {
            continue;
        }
        const long parsed = std::strtol(arg + kPrefixLength, nullptr, 10);
        if (parsed > 0 && parsed <= 100000) {
            return static_cast<std::uint32_t>(parsed);
        }
    }

    return kDefaultFrames;
}

} // namespace

int main(int argc, char** argv) {
    const std::uint32_t maxFrames = ParseMaxFrames(argc, argv);
    HINSTANCE instance = GetModuleHandleW(nullptr);
    std::fprintf(stdout, "kb_render_smoke: start\n");
    std::fflush(stdout);

    kb::render::Renderer renderer;
    SmokeWindowState windowState{.renderer = &renderer};
    HWND window = CreateSmokeWindow(instance, windowState);
    if (window == nullptr) {
        std::fprintf(stderr, "kb_render_smoke: CreateSmokeWindow failed\n");
        std::fflush(stderr);
        return EXIT_FAILURE;
    }
    std::fprintf(stdout, "kb_render_smoke: window created\n");
    std::fflush(stdout);

    Win32RenderSurface surface(window);
    kb::render::DisplayConfig displayConfig{};
    displayConfig.syncMode = kb::render::DisplaySyncMode::VSync;
    displayConfig.targetFps = 180;

    if (!renderer.Initialize(surface, &displayConfig)) {
        std::fprintf(stderr, "kb_render_smoke: renderer.Initialize failed\n");
        std::fflush(stderr);
        DestroyWindow(window);
        return EXIT_FAILURE;
    }
    std::fprintf(stdout, "kb_render_smoke: renderer initialized\n");
    std::fflush(stdout);

    kb::render::SimpleTriangle triangle;
    if (!triangle.Initialize()) {
        std::fprintf(stderr, "kb_render_smoke: triangle.Initialize failed\n");
        std::fflush(stderr);
        renderer.Shutdown();
        DestroyWindow(window);
        return EXIT_FAILURE;
    }
    std::fprintf(stdout, "kb_render_smoke: triangle initialized\n");
    std::fflush(stdout);

    std::uint32_t frameCount = 0;
    bool running = true;
    while (running && frameCount < maxFrames) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
            if (message.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (!running) {
            break;
        }

        if (renderer.BeginFrame()) {
            renderer.SubmitClear(0x101018FFU);
            triangle.Submit();
            renderer.EndFrame();
            ++frameCount;
        }
    }

    triangle.Shutdown();
    renderer.Shutdown();
    std::fprintf(stdout, "kb_render_smoke: renderer shutdown\n");
    std::fflush(stdout);
    DestroyWindow(window);
    UnregisterClassW(kWindowClassName, instance);
    std::fprintf(stdout, "kb_render_smoke: rendered %u frames\n", frameCount);
    std::fflush(stdout);
    return EXIT_SUCCESS;
}
