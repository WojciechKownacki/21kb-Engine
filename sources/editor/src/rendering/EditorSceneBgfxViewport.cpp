#include "rendering/EditorSceneBgfxViewport.hpp"

#if defined(_WIN32)
#include "engine/scene/Scene.hpp"
#include "kb/render/ViewIdPolicy.hpp"
#include "rendering/EditorBgfxBackendSelector.hpp"
#include "rendering/EditorSceneViewportGeometry.hpp"
#include "rendering/EditorSceneViewportRegionBuilder.hpp"

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

namespace kb::editor {
namespace {

constexpr wchar_t kSceneViewportClassName[] = L"KBEditorSceneBgfxViewport";
constexpr std::uint32_t kSceneClearRgba = 0x000000FFU;
constexpr std::uint64_t kEditorSceneAssetDiscoveryIntervalFrames = 600ULL;
constexpr std::uint32_t kMaxEditorViewportIndex =
    (render::ViewId::Max - render::ViewId::DetachedViewportStart) / render::ViewId::DetachedViewportStride;

[[nodiscard]] std::uint32_t RectWidth(const RECT& rect) noexcept {
    return EditorSceneViewportGeometry::RectWidth(rect);
}

[[nodiscard]] std::uint32_t RectHeight(const RECT& rect) noexcept {
    return EditorSceneViewportGeometry::RectHeight(rect);
}

[[nodiscard]] bool RectEquals(const RECT& lhs, const RECT& rhs) noexcept {
    return lhs.left == rhs.left && lhs.top == rhs.top && lhs.right == rhs.right && lhs.bottom == rhs.bottom;
}

[[nodiscard]] RECT CenteredRectFor(const RECT& bounds, std::uint32_t renderWidth, std::uint32_t renderHeight, EditorViewportFitMode fitMode) noexcept {
    return EditorSceneViewportGeometry::CenteredRectFor(bounds, renderWidth, renderHeight, fitMode);
}

[[nodiscard]] RECT ClipRectToClient(HWND parent, const RECT& rect) noexcept {
    return EditorSceneViewportGeometry::ClipRectToClient(parent, rect);
}

[[nodiscard]] RECT ClipRectToBounds(const RECT& bounds, const RECT& rect) noexcept {
    RECT clipped{};
    if (IntersectRect(&clipped, &rect, &bounds) == 0) {
        return {};
    }
    return clipped;
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

void EditorSceneBgfxViewport::Configure(HINSTANCE instance, HWND parent, EditorRenderBackendSettings* backendSettings) noexcept {
    instance_ = instance;
    defaultParent_ = parent;
    backendSettings_ = backendSettings;
}

const char* EditorSceneBgfxViewport::ActiveBackendLabel() const noexcept {
    if (!renderer_.IsInitialized()) {
        return "Not initialized";
    }
    return renderer_.CapabilityReport().selectedBackendName;
}

void EditorSceneBgfxViewport::RequestPresent() noexcept {
    presentRequested_ = true;
}

bool EditorSceneBgfxViewport::PresentRequested() const noexcept {
    return presentRequested_;
}

void EditorSceneBgfxViewport::ClearPresentRequest() noexcept {
    presentRequested_ = false;
}

void EditorSceneBgfxViewport::SyncHostSurfaceLayouts(HWND parent, std::span<const HostSurfaceLayout> layouts) noexcept {
    if (parent == nullptr) {
        return;
    }

    hostSurfaceStore_.MarkHostNotPresented(parent);
    for (const HostSurfaceLayout& layout : layouts) {
        const RECT layoutBounds = ClipRectToClient(parent, layout.bounds);
        if (RectWidth(layoutBounds) == 0U || RectHeight(layoutBounds) == 0U) {
            continue;
        }

        HostSurface* surface = hostSurfaceStore_.Ensure(parent, layout.viewportKey);
        if (surface == nullptr) {
            continue;
        }

        const bool layoutChanged = !surface->hasLayoutBounds || !RectEquals(surface->layoutBounds, layoutBounds);
        if (layoutChanged) {
            surface->layoutBounds = layoutBounds;
            surface->hasLayoutBounds = true;
            if (surface->window != nullptr && IsWindow(surface->window) != 0) {
                static_cast<void>(EnsureHostSurfaceWindow(*surface, layoutBounds));
            }
            RequestPresent();
        }

        if (surface->window != nullptr && IsWindow(surface->window) != 0 && IsWindowVisible(surface->window) == 0) {
            RequestPresent();
        }
        surface->presentedInCurrentPaint = true;
    }

    if (hostSurfaceStore_.HasVisibleUnpresentedForHost(parent)) {
        RequestPresent();
    }
}

void EditorSceneBgfxViewport::Shutdown() {
    ShutdownGpuResources();

    sessionStore_.Clear();
    hostSurfaceStore_.Clear();

    if (windowClassRegistered_ && instance_ != nullptr) {
        UnregisterClassW(kSceneViewportClassName, instance_);
        windowClassRegistered_ = false;
    }

    paintParent_ = nullptr;
    contextWindow_ = nullptr;
    backendSettings_ = nullptr;
    rendererBackendGeneration_ = 0;
    pendingPresents_.clear();
    pendingSubmissions_.clear();
    presentRequested_ = true;
    renderFailed_ = false;
    renderFailureReported_ = false;
}

void EditorSceneBgfxViewport::BeginPaintLayout() noexcept {
    BeginPaintLayout(defaultParent_);
}

void EditorSceneBgfxViewport::BeginPaintLayout(HWND parent) noexcept {
    paintParent_ = parent;
    pendingPresents_.clear();
    pendingSubmissions_.clear();
    sessionStore_.MarkHostNotPresented(parent);
    hostSurfaceStore_.MarkHostNotPresented(parent);
}

void EditorSceneBgfxViewport::EndPaintLayout() {
    if (!renderFailed_ && !SubmitPendingPaint()) {
        FailRender("Scene render/present failed during queued viewport submit. The editor will stay open, but the scene viewport was disabled.");
    }

    hostSurfaceStore_.HideUnpresentedForHost(paintParent_);
    pendingPresents_.clear();
    pendingSubmissions_.clear();
    paintParent_ = nullptr;
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

    if (renderFailed_) {
        return;
    }

    if (parent == nullptr || RectWidth(rect) == 0 || RectHeight(rect) == 0) {
        HideSession(parent, settings.viewportKey);
        return;
    }

    ViewportSession* session = EnsureSession(parent, settings.viewportKey);
    if (session == nullptr) {
        return;
    }
    session->presentedInCurrentPaint = true;

    if (!RenderAndPresent(dc, rect, *session, scene, settings)) {
        FailRender("Scene render/present failed while queuing viewport present. The editor will stay open, but the scene viewport was disabled.");
    }
}

void EditorSceneBgfxViewport::Present(HWND parent, const RECT& rect, const kb::scene::Scene& scene, const PresentSettings& settings) {
    Present(nullptr, parent, rect, scene, EditorTheme{}, settings);
}

void EditorSceneBgfxViewport::Hide() noexcept {
    sessionStore_.MarkAllNotPresented();
}

EditorSceneBgfxViewport::ViewportSession* EditorSceneBgfxViewport::EnsureSession(HWND host, std::uint64_t key) {
    return sessionStore_.Ensure(host, key, kMaxEditorViewportIndex);
}

EditorSceneBgfxViewport::ViewportSession* EditorSceneBgfxViewport::FindSession(HWND host, std::uint64_t key) noexcept {
    return sessionStore_.Find(host, key);
}

EditorSceneBgfxViewport::ViewportSession* EditorSceneBgfxViewport::FindSessionByKey(std::uint64_t key) noexcept {
    return sessionStore_.FindByKey(key);
}

EditorSceneBgfxViewport::HostSurface* EditorSceneBgfxViewport::EnsureHostSurface(HWND host, std::uint64_t key) {
    return hostSurfaceStore_.Ensure(host, key);
}

EditorSceneBgfxViewport::HostSurface* EditorSceneBgfxViewport::FindHostSurface(HWND host, std::uint64_t key) noexcept {
    return hostSurfaceStore_.Find(host, key);
}

bool EditorSceneBgfxViewport::EnsureWindowClass() {
    if (windowClassRegistered_) {
        return true;
    }
    if (instance_ == nullptr) {
        return false;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = &EditorSceneBgfxViewport::WindowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = kSceneViewportClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        return false;
    }
    windowClassRegistered_ = true;
    return true;
}

bool EditorSceneBgfxViewport::EnsureContextWindow() {
    if (contextWindow_ != nullptr && IsWindow(contextWindow_) != 0) {
        return true;
    }
    if (defaultParent_ == nullptr || !EnsureWindowClass()) {
        return false;
    }

    contextWindow_ = CreateWindowExW(
        0,
        kSceneViewportClassName,
        L"",
        WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0,
        0,
        1,
        1,
        defaultParent_,
        nullptr,
        instance_,
        this);
    if (contextWindow_ == nullptr) {
        return false;
    }

    ShowWindow(contextWindow_, SW_HIDE);
    return true;
}

bool EditorSceneBgfxViewport::EnsureHostSurfaceWindow(HostSurface& surface, const RECT& rect) {
    if (surface.host == nullptr || RectWidth(rect) == 0U || RectHeight(rect) == 0U || !EnsureWindowClass()) {
        return false;
    }

    bool needsPositionUpdate = !RectEquals(surface.rect, rect);
    bool needsRegionUpdate = needsPositionUpdate;
    if (surface.window == nullptr || IsWindow(surface.window) == 0) {
        surface.window = CreateWindowExW(
            0,
            kSceneViewportClassName,
            L"",
            WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
            rect.left,
            rect.top,
            static_cast<int>(RectWidth(rect)),
            static_cast<int>(RectHeight(rect)),
            surface.host,
            nullptr,
            instance_,
            this);
        if (surface.window == nullptr) {
            return false;
        }
        needsPositionUpdate = false;
        needsRegionUpdate = true;
    } else if (GetParent(surface.window) != surface.host) {
        ShowWindow(surface.window, SW_HIDE);
        SetParent(surface.window, surface.host);
        needsPositionUpdate = true;
        needsRegionUpdate = true;
    }

    if (needsPositionUpdate) {
        SetWindowPos(
            surface.window,
            HWND_BOTTOM,
            rect.left,
            rect.top,
            static_cast<int>(RectWidth(rect)),
            static_cast<int>(RectHeight(rect)),
            SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOCOPYBITS);
    }

    if (needsRegionUpdate) {
        HRGN combinedRegion = EditorSceneViewportRegionBuilder::BuildCombinedRegion(rect, std::span<const RECT>{&rect, 1U});
        if (combinedRegion == nullptr) {
            return false;
        }
        if (SetWindowRgn(surface.window, combinedRegion, FALSE) == 0) {
            DeleteObject(combinedRegion);
            return false;
        }
    }

    surface.rect = rect;
    return true;
}

bool EditorSceneBgfxViewport::EnsureRenderer() {
    if (renderer_.IsInitialized()) {
        const std::uint64_t requestedGeneration = backendSettings_ == nullptr ? 0U : backendSettings_->Generation();
        if (rendererBackendGeneration_ == requestedGeneration) {
            return true;
        }
        ShutdownGpuResources();
    }
    if (!EnsureContextWindow()) {
        return false;
    }

    Win32Surface surface(contextWindow_);
    render::DisplayConfig config{};
    config.syncMode = render::DisplaySyncMode::Uncapped;
    config.targetFps = 180;
    config.flushAfterRender = false;
    bgfx::RendererType::Enum supportedBackends[bgfx::RendererType::Count]{};
    const std::uint8_t supportedBackendCount = bgfx::getSupportedRenderers(static_cast<std::uint8_t>(bgfx::RendererType::Count), supportedBackends);
    const bgfx::RendererType::Enum preferredBackend = EditorBgfxBackendSelector::Resolve(supportedBackends, supportedBackendCount, backendSettings_);
    config.preferredBgfxRendererType = preferredBackend == bgfx::RendererType::Count ? -1 : static_cast<std::int32_t>(preferredBackend);

    if (!renderer_.Initialize(surface, &config)) {
        return false;
    }

    renderer_.SetRuntimeAssetDiscoveryIntervalFrames(kEditorSceneAssetDiscoveryIntervalFrames);
    rendererBackendGeneration_ = backendSettings_ == nullptr ? 0U : backendSettings_->Generation();
    return true;
}

bool EditorSceneBgfxViewport::EnsurePresentTarget(HostSurface& surface, std::uint32_t width, std::uint32_t height) {
    if (surface.window == nullptr || !renderer_.IsInitialized()) {
        return false;
    }
    return surface.presentTarget.Ensure(render::NativeWindowFramebufferDesc{
        .nativeWindow = surface.window,
        .width = width,
        .height = height,
        .colorFormat = bgfx::TextureFormat::BGRA8,
        .depthFormat = bgfx::TextureFormat::Count,
        .flushBeforeRecreate = true,
    });
}

void EditorSceneBgfxViewport::HideHostSurface(HostSurface& surface) noexcept {
    hostSurfaceStore_.Hide(surface);
}

void EditorSceneBgfxViewport::HideSession(HWND host, std::uint64_t key) noexcept {
    ViewportSession* session = FindSession(host, key);
    if (session != nullptr) {
        session->presentedInCurrentPaint = false;
        if (HostSurface* surface = FindHostSurface(session->host, session->key); surface != nullptr) {
            HideHostSurface(*surface);
        }
    }
}

void EditorSceneBgfxViewport::ReleaseWindow(HWND window) noexcept {
    if (window == contextWindow_) {
        contextWindow_ = nullptr;
        return;
    }

    hostSurfaceStore_.ReleaseWindow(window);
}

void EditorSceneBgfxViewport::ShutdownGpuResources() noexcept {
    ShutdownSessionFramebuffers();
    renderer_.Shutdown();
    hostSurfaceStore_.DestroyWindows();
    if (contextWindow_ != nullptr && IsWindow(contextWindow_) != 0) {
        const HWND window = contextWindow_;
        contextWindow_ = nullptr;
        DestroyWindow(window);
    } else {
        contextWindow_ = nullptr;
    }
}

void EditorSceneBgfxViewport::ShutdownSessionFramebuffers() noexcept {
    sessionStore_.ShutdownFramebuffers();
    hostSurfaceStore_.ShutdownPresentTargets();
}

bool EditorSceneBgfxViewport::SubmitPendingPaint() {
    pendingSubmissions_.clear();
    if (pendingPresents_.empty()) {
        return true;
    }

    PendingPaintSubmitter submitter(*this);
    return submitter.Submit(std::span<const PendingPresent>{pendingPresents_.data(), pendingPresents_.size()});
}

void EditorSceneBgfxViewport::FailRender(const char* reason) noexcept {
    renderFailed_ = true;
    pendingPresents_.clear();
    pendingSubmissions_.clear();
    ShutdownSessionFramebuffers();
    hostSurfaceStore_.DestroyWindows();

    if (renderFailureReported_) {
        return;
    }

    renderFailureReported_ = true;
    MessageBoxA(nullptr, reason == nullptr ? "Scene render failed. The editor will stay open, but the scene viewport was disabled." : reason, "21kb Editor - Scene Render Failed", MB_OK | MB_ICONERROR);
}

bool EditorSceneBgfxViewport::RenderAndPresent(HDC dc, const RECT& rect, ViewportSession& session, const kb::scene::Scene& scene, const PresentSettings& settings) {
    static_cast<void>(dc);
    return QueuePresent(rect, session, scene, settings);
}

bool EditorSceneBgfxViewport::QueuePresent(const RECT& rect, ViewportSession& session, const kb::scene::Scene& scene, const PresentSettings& settings) {
    RECT requestedPanel = rect;
    if (HostSurface* surface = FindHostSurface(session.host, session.key); surface != nullptr && surface->hasLayoutBounds) {
        requestedPanel = ClipRectToBounds(surface->layoutBounds, requestedPanel);
    }
    const RECT clippedPanel = ClipRectToClient(session.host, requestedPanel);
    const std::uint32_t panelWidth = RectWidth(clippedPanel);
    const std::uint32_t panelHeight = RectHeight(clippedPanel);
    if (panelWidth == 0U || panelHeight == 0U) {
        HideSession(session.host, session.key);
        return true;
    }
    const std::uint32_t width = settings.renderWidth == 0U ? panelWidth : settings.renderWidth;
    const std::uint32_t height = settings.renderHeight == 0U ? panelHeight : settings.renderHeight;
    const RECT destination = ClipRectToBounds(clippedPanel, CenteredRectFor(clippedPanel, width, height, settings.fitMode));
    const std::uint32_t outputWidth = RectWidth(destination);
    const std::uint32_t outputHeight = RectHeight(destination);
    if (outputWidth == 0U || outputHeight == 0U) {
        HideSession(session.host, session.key);
        return true;
    }
    if (!EnsureRenderer()) {
        return false;
    }

    pendingPresents_.push_back(PendingPresent{
        .session = &session,
        .host = session.host,
        .surfaceRect = clippedPanel,
        .destination = destination,
        .scene = &scene,
        .settings = settings,
        .renderWidth = width,
        .renderHeight = height,
        .outputWidth = outputWidth,
        .outputHeight = outputHeight,
    });
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
    case WM_ERASEBKGND: {
        RECT client{};
        if (GetClientRect(window, &client) != 0) {
            FillRect(reinterpret_cast<HDC>(wparam), &client, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        }
        return TRUE;
    }
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
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
