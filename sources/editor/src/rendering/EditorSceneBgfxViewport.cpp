#include "rendering/EditorSceneBgfxViewport.hpp"

#if defined(_WIN32)
#include "engine/scene/Scene.hpp"
#include "kb/render/ViewIdPolicy.hpp"

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <span>
#include <vector>

namespace kb::editor {
namespace {

constexpr wchar_t kSceneViewportClassName[] = L"KBEditorSceneBgfxViewport";
constexpr std::uint32_t kSceneClearRgba = 0x000000FFU;
constexpr std::uint32_t kMaxEditorViewportIndex =
    (render::ViewId::Max - render::ViewId::DetachedViewportStart) / render::ViewId::DetachedViewportStride;

[[nodiscard]] std::uint32_t RectWidth(const RECT& rect) noexcept {
    return static_cast<std::uint32_t>(std::max<LONG>(0, rect.right - rect.left));
}

[[nodiscard]] std::uint32_t RectHeight(const RECT& rect) noexcept {
    return static_cast<std::uint32_t>(std::max<LONG>(0, rect.bottom - rect.top));
}

[[nodiscard]] RECT CenteredRectFor(const RECT& bounds, std::uint32_t renderWidth, std::uint32_t renderHeight, EditorViewportFitMode fitMode) noexcept {
    const std::uint32_t boundsWidth = RectWidth(bounds);
    const std::uint32_t boundsHeight = RectHeight(bounds);
    if (boundsWidth == 0U || boundsHeight == 0U || renderWidth == 0U || renderHeight == 0U) {
        return bounds;
    }

    double scale = 1.0;
    const double scaleX = static_cast<double>(boundsWidth) / static_cast<double>(renderWidth);
    const double scaleY = static_cast<double>(boundsHeight) / static_cast<double>(renderHeight);
    switch (fitMode) {
    case EditorViewportFitMode::Fit:
        scale = std::min(scaleX, scaleY);
        break;
    case EditorViewportFitMode::OneToOne:
        scale = 1.0;
        break;
    case EditorViewportFitMode::Fill:
        scale = std::max(scaleX, scaleY);
        break;
    }

    const LONG width = std::max<LONG>(1, static_cast<LONG>(std::lround(static_cast<double>(renderWidth) * scale)));
    const LONG height = std::max<LONG>(1, static_cast<LONG>(std::lround(static_cast<double>(renderHeight) * scale)));
    const LONG centerX = bounds.left + static_cast<LONG>(boundsWidth / 2U);
    const LONG centerY = bounds.top + static_cast<LONG>(boundsHeight / 2U);
    return RECT{
        .left = centerX - width / 2,
        .top = centerY - height / 2,
        .right = centerX - width / 2 + width,
        .bottom = centerY - height / 2 + height,
    };
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
}

void EditorSceneBgfxViewport::Shutdown() {
    ShutdownGpuResources();

    sessions_.clear();

    if (windowClassRegistered_ && instance_ != nullptr) {
        UnregisterClassW(kSceneViewportClassName, instance_);
        windowClassRegistered_ = false;
    }

    paintParent_ = nullptr;
    deviceWindow_ = nullptr;
    nextDetachedViewportIndex_ = 1;
}

void EditorSceneBgfxViewport::BeginPaintLayout() noexcept {
    BeginPaintLayout(defaultParent_);
}

void EditorSceneBgfxViewport::BeginPaintLayout(HWND parent) noexcept {
    paintParent_ = parent;
    if (ViewportSession* session = FindSession(parent); session != nullptr) {
        session->presentedInCurrentPaint = false;
    }
}

void EditorSceneBgfxViewport::EndPaintLayout() {
    ViewportSession* session = FindSession(paintParent_);
    if (session != nullptr && !session->presentedInCurrentPaint) {
        HideSession(paintParent_);
    }
}

void EditorSceneBgfxViewport::Present(HDC dc, const RECT& rect, const kb::scene::Scene& scene, const EditorTheme& theme) {
    Present(dc, defaultParent_, rect, scene, theme);
}

void EditorSceneBgfxViewport::Present(HDC dc, HWND parent, const RECT& rect, const kb::scene::Scene& scene, const EditorTheme& theme) {
    Present(dc, parent, rect, scene, theme, PresentSettings{});
}

void EditorSceneBgfxViewport::Present(HDC dc, const RECT& rect, const kb::scene::Scene& scene, const EditorTheme& theme, const PresentSettings& settings) {
    Present(dc, defaultParent_, rect, scene, theme, settings);
}

void EditorSceneBgfxViewport::Present(HDC dc, HWND parent, const RECT& rect, const kb::scene::Scene& scene, const EditorTheme& theme, const PresentSettings& settings) {
    static_cast<void>(theme);

    if (parent == nullptr || RectWidth(rect) == 0 || RectHeight(rect) == 0) {
        HideSession(parent);
        return;
    }

    ViewportSession* session = EnsureSession(parent);
    if (session == nullptr) {
        return;
    }
    session->presentedInCurrentPaint = true;

    if (!RenderAndPresent(dc, rect, *session, scene, settings)) {
        FailRender("Scene render/present failed");
    }
}

void EditorSceneBgfxViewport::Hide() noexcept {
    for (const std::unique_ptr<ViewportSession>& session : sessions_) {
        if (session != nullptr) {
            session->presentedInCurrentPaint = false;
        }
    }
}

EditorSceneBgfxViewport::ViewportSession* EditorSceneBgfxViewport::EnsureSession(HWND host) {
    if (host == nullptr) {
        return nullptr;
    }

    if (ViewportSession* existing = FindSession(host); existing != nullptr) {
        return existing;
    }

    std::uint32_t viewportIndex = 0U;
    if (host != defaultParent_) {
        viewportIndex = nextDetachedViewportIndex_;
        if (viewportIndex > kMaxEditorViewportIndex) {
            return nullptr;
        }
        ++nextDetachedViewportIndex_;
    }

    std::unique_ptr<ViewportSession> session = std::make_unique<ViewportSession>();
    session->host = host;
    session->viewportIndex = viewportIndex;
    sessions_.push_back(std::move(session));
    return sessions_.back().get();
}

EditorSceneBgfxViewport::ViewportSession* EditorSceneBgfxViewport::FindSession(HWND host) noexcept {
    if (host == nullptr) {
        return nullptr;
    }

    const auto iter = std::ranges::find_if(sessions_, [host](const std::unique_ptr<ViewportSession>& session) {
        return session != nullptr && session->host == host;
    });
    return iter == sessions_.end() ? nullptr : iter->get();
}

bool EditorSceneBgfxViewport::EnsureDeviceWindow(HWND parent, const RECT& rect) {
    if (deviceWindow_ != nullptr && IsWindow(deviceWindow_) != 0) {
        if (GetParent(deviceWindow_) != parent) {
            SetParent(deviceWindow_, parent);
        }
        SetWindowPos(
            deviceWindow_,
            HWND_TOP,
            rect.left,
            rect.top,
            static_cast<int>(RectWidth(rect)),
            static_cast<int>(RectHeight(rect)),
            SWP_SHOWWINDOW);
        return true;
    }

    if (instance_ == nullptr || parent == nullptr || RectWidth(rect) == 0U || RectHeight(rect) == 0U) {
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

    deviceWindow_ = CreateWindowExW(
        0,
        kSceneViewportClassName,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        rect.left,
        rect.top,
        static_cast<int>(RectWidth(rect)),
        static_cast<int>(RectHeight(rect)),
        parent,
        nullptr,
        instance_,
        this);
    return deviceWindow_ != nullptr;
}

bool EditorSceneBgfxViewport::EnsureRenderer(HWND parent, const RECT& rect) {
    if (!EnsureDeviceWindow(parent, rect)) {
        return false;
    }

    if (renderer_.IsInitialized()) {
        renderer_.OnResize(RectWidth(rect), RectHeight(rect));
        return true;
    }

    Win32Surface surface(deviceWindow_);
    render::DisplayConfig config{};
    config.syncMode = render::DisplaySyncMode::VSync;
    config.targetFps = 120;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Direct3D11);

    if (!renderer_.Initialize(surface, &config)) {
        return false;
    }

    InvalidateRect(deviceWindow_, nullptr, FALSE);
    return true;
}

void EditorSceneBgfxViewport::HideSession(HWND host) noexcept {
    ViewportSession* session = FindSession(host);
    if (session != nullptr) {
        session->presentedInCurrentPaint = false;
    }
    if (deviceWindow_ != nullptr && IsWindow(deviceWindow_) != 0 && (host == nullptr || GetParent(deviceWindow_) == host)) {
        ShowWindow(deviceWindow_, SW_HIDE);
    }
}

void EditorSceneBgfxViewport::ReleaseWindow(HWND window) noexcept {
    if (window == deviceWindow_) {
        for (const std::unique_ptr<ViewportSession>& session : sessions_) {
            if (session != nullptr) {
                session->sceneTarget.Shutdown();
                session->postProcessTargets.Shutdown();
            }
        }
    }
}

void EditorSceneBgfxViewport::ShutdownGpuResources() noexcept {
    ShutdownSessionFramebuffers();
    renderer_.Shutdown();
    if (deviceWindow_ != nullptr && IsWindow(deviceWindow_) != 0) {
        const HWND window = deviceWindow_;
        deviceWindow_ = nullptr;
        DestroyWindow(window);
    } else {
        deviceWindow_ = nullptr;
    }
}

void EditorSceneBgfxViewport::ShutdownSessionFramebuffers() noexcept {
    for (const std::unique_ptr<ViewportSession>& session : sessions_) {
        if (session != nullptr) {
            session->sceneTarget.Shutdown();
            session->postProcessTargets.Shutdown();
        }
    }
}

void EditorSceneBgfxViewport::FailRender(const char* reason) noexcept {
    MessageBoxA(nullptr, reason == nullptr ? "Scene render failed" : reason, "21kb Editor - Scene Render Fatal", MB_OK | MB_ICONERROR);
    std::abort();
}

bool EditorSceneBgfxViewport::RenderAndPresent(HDC dc, const RECT& rect, ViewportSession& session, const kb::scene::Scene& scene, const PresentSettings& settings) {
    static_cast<void>(dc);
    const std::uint32_t panelWidth = RectWidth(rect);
    const std::uint32_t panelHeight = RectHeight(rect);
    const std::uint32_t width = settings.renderWidth == 0U ? panelWidth : settings.renderWidth;
    const std::uint32_t height = settings.renderHeight == 0U ? panelHeight : settings.renderHeight;
    const RECT destination = CenteredRectFor(rect, width, height, settings.fitMode);
    const std::uint32_t outputWidth = RectWidth(destination);
    const std::uint32_t outputHeight = RectHeight(destination);
    if (!EnsureRenderer(session.host, destination)) {
        return false;
    }

    render::Renderer::SceneFrameSubmission submission{};
    if (!BuildSubmission(session, scene, width, height, outputWidth, outputHeight, settings, submission)) {
        return false;
    }

    if (!renderer_.BeginFrame()) {
        return false;
    }

    const bool submitted = renderer_.SubmitScene(*submission.scene, submission.desc);
    renderer_.EndFrame();
    if (!submitted) {
        return false;
    }

    return true;
}

bool EditorSceneBgfxViewport::BuildSubmission(ViewportSession& session, const kb::scene::Scene& scene, std::uint32_t renderWidth, std::uint32_t renderHeight, std::uint32_t outputWidth, std::uint32_t outputHeight, const PresentSettings& settings, render::Renderer::SceneFrameSubmission& submission) {
    if (renderWidth == 0U || renderHeight == 0U || outputWidth == 0U || outputHeight == 0U) {
        return false;
    }

    if (!session.sceneTarget.Ensure(render::SceneRenderTargetDesc{
            .extent = render::RenderExtent{renderWidth, renderHeight},
            .colorPolicy = render::SceneColorFormatPolicy::Auto,
        })) {
        return false;
    }
    if (!session.postProcessTargets.Ensure(render::ScenePostProcessTargetsDesc{
            .extent = render::RenderExtent{renderWidth, renderHeight},
            .colorPolicy = render::SceneColorFormatPolicy::Auto,
        })) {
        return false;
    }

    submission = render::Renderer::SceneFrameSubmission{
        .scene = &scene,
        .desc = render::RenderSceneSubmitDesc{
        .target = render::RenderSceneTargetBinding{
            .frameBuffer = session.sceneTarget.FrameBuffer(),
            .colorTexture = session.sceneTarget.ColorTexture(),
            .viewport = render::RenderViewportDesc{
                .id = render::RenderViewportId{ session.viewportIndex + 1U },
                .extent = render::RenderExtent{ renderWidth, renderHeight },
                .viewportIndex = session.viewportIndex,
            },
        },
        .postProcess = session.postProcessTargets.Binding(),
        .finalComposite = render::RenderFinalCompositeTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .extent = render::RenderExtent{ outputWidth, outputHeight },
            .enabled = true,
        },
        .cameraOverride = settings.cameraOverride,
        .selectedEntityIds = settings.selectedEntityIds[0] == 0U
            ? std::span<const std::uint64_t>{}
            : std::span<const std::uint64_t>{settings.selectedEntityIds.data(), settings.selectedEntityIds.size()},
        .clearRgba = kSceneClearRgba,
        },
    };
    return true;
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
        return 0;
    }
    case WM_NCDESTROY:
        if (viewport != nullptr) {
            viewport->ReleaseWindow(window);
        }
        break;
    default:
        break;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace kb::editor

#endif
