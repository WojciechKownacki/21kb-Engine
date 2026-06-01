#include "rendering/EditorSceneBgfxViewport.hpp"

#if defined(_WIN32)
#include <bgfx/bgfx.h>

#include <algorithm>
#include <cstdint>

namespace kb::editor {
namespace {

constexpr wchar_t kSceneViewportClassName[] = L"KBEditorSceneBgfxViewport";

[[nodiscard]] bool EqualRectValue(const RECT& lhs, const RECT& rhs) noexcept {
    return lhs.left == rhs.left && lhs.top == rhs.top && lhs.right == rhs.right && lhs.bottom == rhs.bottom;
}

[[nodiscard]] std::uint32_t RectWidth(const RECT& rect) noexcept {
    return static_cast<std::uint32_t>(std::max<LONG>(0, rect.right - rect.left));
}

[[nodiscard]] std::uint32_t RectHeight(const RECT& rect) noexcept {
    return static_cast<std::uint32_t>(std::max<LONG>(0, rect.bottom - rect.top));
}

} // namespace

EditorSceneBgfxViewport::Win32Surface::Win32Surface(HWND window) noexcept
    : window_(window) {}

std::uint32_t EditorSceneBgfxViewport::Win32Surface::Width() const noexcept {
    RECT rect{};
    if (GetClientRect(window_, &rect) == 0) {
        return 0;
    }
    return RectWidth(rect);
}

std::uint32_t EditorSceneBgfxViewport::Win32Surface::Height() const noexcept {
    RECT rect{};
    if (GetClientRect(window_, &rect) == 0) {
        return 0;
    }
    return RectHeight(rect);
}

void* EditorSceneBgfxViewport::Win32Surface::NativeWindowHandle() const noexcept {
    return window_;
}

void* EditorSceneBgfxViewport::Win32Surface::NativeDisplayHandle() const noexcept {
    return nullptr;
}

EditorSceneBgfxViewport::~EditorSceneBgfxViewport() {
    Shutdown();
}

void EditorSceneBgfxViewport::Configure(HINSTANCE instance, HWND parent) noexcept {
    instance_ = instance;
    defaultParent_ = parent;
    parent_ = parent;
}

void EditorSceneBgfxViewport::Shutdown() {
    triangle_.Shutdown();
    triangleReady_ = false;
    renderer_.Shutdown();

    if (window_ != nullptr) {
        DestroyWindow(window_);
        window_ = nullptr;
    }

    if (windowClassRegistered_ && instance_ != nullptr) {
        UnregisterClassW(kSceneViewportClassName, instance_);
        windowClassRegistered_ = false;
    }

    hasRect_ = false;
    hasQueuedRect_ = false;
    lastRect_ = RECT{};
    queuedRect_ = RECT{};
    paintParent_ = nullptr;
    queuedParent_ = nullptr;
}

void EditorSceneBgfxViewport::BeginPaintLayout() noexcept {
    BeginPaintLayout(defaultParent_);
}

void EditorSceneBgfxViewport::BeginPaintLayout(HWND parent) noexcept {
    hasQueuedRect_ = false;
    paintParent_ = parent;
    queuedParent_ = nullptr;
}

void EditorSceneBgfxViewport::QueuePresent(const RECT& rect) noexcept {
    QueuePresent(defaultParent_, rect);
}

void EditorSceneBgfxViewport::QueuePresent(HWND parent, const RECT& rect) noexcept {
    queuedRect_ = rect;
    queuedParent_ = parent;
    hasQueuedRect_ = true;
}

void EditorSceneBgfxViewport::FlushQueuedPresent() {
    if (hasQueuedRect_) {
        Present(queuedParent_, queuedRect_);
        hasQueuedRect_ = false;
        return;
    }

    if (parent_ == paintParent_) {
        Hide();
    }
}

void EditorSceneBgfxViewport::Present(const RECT& rect) {
    Present(defaultParent_, rect);
}

void EditorSceneBgfxViewport::Present(HWND parent, const RECT& rect) {
    if (parent == nullptr || RectWidth(rect) == 0 || RectHeight(rect) == 0) {
        Hide();
        return;
    }

    if (!UseParent(parent)) {
        Hide();
        return;
    }

    if (!EnsureWindow()) {
        return;
    }

    MoveTo(rect);
    if (!EnsureRenderer()) {
        return;
    }

    RenderFrame();
}

void EditorSceneBgfxViewport::Hide() noexcept {
    if (window_ != nullptr) {
        ShowWindow(window_, SW_HIDE);
    }
}

bool EditorSceneBgfxViewport::UseParent(HWND parent) noexcept {
    if (parent == nullptr) {
        return false;
    }

    if (parent_ == parent) {
        return true;
    }

    parent_ = parent;
    hasRect_ = false;

    if (window_ == nullptr) {
        return true;
    }

    SetParent(window_, parent_);
    return GetParent(window_) == parent_;
}

bool EditorSceneBgfxViewport::EnsureWindow() {
    if (window_ != nullptr) {
        return true;
    }
    if (instance_ == nullptr || parent_ == nullptr) {
        return false;
    }

    if (!windowClassRegistered_) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        windowClass.lpfnWndProc = &EditorSceneBgfxViewport::WindowProc;
        windowClass.hInstance = instance_;
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        windowClass.lpszClassName = kSceneViewportClassName;
        if (RegisterClassExW(&windowClass) == 0) {
            return false;
        }
        windowClassRegistered_ = true;
    }

    window_ = CreateWindowExW(
        0,
        kSceneViewportClassName,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0,
        0,
        1,
        1,
        parent_,
        nullptr,
        instance_,
        this);

    return window_ != nullptr;
}

bool EditorSceneBgfxViewport::EnsureRenderer() {
    if (renderer_.IsInitialized()) {
        return true;
    }

    Win32Surface surface(window_);
    render::DisplayConfig config{};
    config.syncMode = render::DisplaySyncMode::VSync;
    config.targetFps = 120;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Direct3D11);

    if (!renderer_.Initialize(surface, &config)) {
        return false;
    }

    triangleReady_ = triangle_.Initialize();
    InvalidateRect(window_, nullptr, FALSE);
    return true;
}

void EditorSceneBgfxViewport::MoveTo(const RECT& rect) {
    if (window_ == nullptr) {
        return;
    }

    if (!hasRect_ || !EqualRectValue(lastRect_, rect)) {
        const std::uint32_t width = RectWidth(rect);
        const std::uint32_t height = RectHeight(rect);
        MoveWindow(window_, rect.left, rect.top, static_cast<int>(width), static_cast<int>(height), TRUE);
        ShowWindow(window_, SW_SHOW);
        if (renderer_.IsInitialized()) {
            renderer_.OnResize(width, height);
        }
        lastRect_ = rect;
        hasRect_ = true;
    }
}

void EditorSceneBgfxViewport::RenderFrame() {
    if (!renderer_.BeginFrame()) {
        return;
    }

    renderer_.SubmitClear(0x14202AFFU);
    if (triangleReady_) {
        triangle_.Submit();
    } else {
        bgfx::dbgTextClear();
        bgfx::dbgTextPrintf(2, 1, 0x0C, "bgfx active");
        bgfx::dbgTextPrintf(2, 2, 0x0C, "triangle shader not found");
    }
    renderer_.EndFrame();
}

LRESULT CALLBACK EditorSceneBgfxViewport::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* viewport = reinterpret_cast<EditorSceneBgfxViewport*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
    case WM_NCCREATE: {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        EndPaint(window, &paint);
        if (viewport != nullptr) {
            viewport->RenderFrame();
        }
        return 0;
    }
    case WM_SIZE:
        if (viewport != nullptr && wparam != SIZE_MINIMIZED && viewport->renderer_.IsInitialized()) {
            viewport->renderer_.OnResize(static_cast<std::uint32_t>(LOWORD(lparam)), static_cast<std::uint32_t>(HIWORD(lparam)));
            viewport->RenderFrame();
        }
        return 0;
    default:
        break;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace kb::editor

#endif
