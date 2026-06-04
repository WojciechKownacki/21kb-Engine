#include "rendering/EditorSceneBgfxViewport.hpp"

#if defined(_WIN32)
#include "engine/scene/Scene.hpp"
#include "kb/render/ViewIdPolicy.hpp"
#include "rendering/EditorBgfxBackendSelector.hpp"

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

[[nodiscard]] RECT ClipRectToClient(HWND parent, const RECT& rect) noexcept {
    if (parent == nullptr) {
        return {};
    }
    RECT client{};
    if (GetClientRect(parent, &client) == 0) {
        return {};
    }
    RECT clipped{};
    if (IntersectRect(&clipped, &rect, &client) == 0) {
        return {};
    }
    return clipped;
}

[[nodiscard]] bool SameRect(const RECT& lhs, const RECT& rhs) noexcept {
    return lhs.left == rhs.left && lhs.top == rhs.top && lhs.right == rhs.right && lhs.bottom == rhs.bottom;
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

void EditorSceneBgfxViewport::Shutdown() {
    ShutdownGpuResources();

    sessions_.clear();
    hostSurfaces_.clear();

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
    nextViewportIndex_ = 0;
}

void EditorSceneBgfxViewport::BeginPaintLayout() noexcept {
    BeginPaintLayout(defaultParent_);
}

void EditorSceneBgfxViewport::BeginPaintLayout(HWND parent) noexcept {
    paintParent_ = parent;
    pendingPresents_.clear();
    pendingSubmissions_.clear();
    for (const std::unique_ptr<ViewportSession>& session : sessions_) {
        if (session != nullptr && session->host == parent) {
            session->presentedInCurrentPaint = false;
        }
    }
    for (const std::unique_ptr<HostSurface>& surface : hostSurfaces_) {
        if (surface != nullptr && surface->host == parent) {
            surface->presentedInCurrentPaint = false;
        }
    }
}

void EditorSceneBgfxViewport::EndPaintLayout() {
    if (!SubmitPendingPaint()) {
        FailRender("Scene render/present failed");
    }

    for (const std::unique_ptr<HostSurface>& surface : hostSurfaces_) {
        if (surface != nullptr && surface->host == paintParent_ && !surface->presentedInCurrentPaint) {
            HideHostSurface(*surface);
        }
    }
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

EditorSceneBgfxViewport::ViewportSession* EditorSceneBgfxViewport::EnsureSession(HWND host, std::uint64_t key) {
    if (host == nullptr) {
        return nullptr;
    }

    if (ViewportSession* existing = key == 0U ? FindSession(host, key) : FindSessionByKey(key); existing != nullptr) {
        existing->host = host;
        return existing;
    }

    const std::uint32_t viewportIndex = nextViewportIndex_;
    if (viewportIndex > kMaxEditorViewportIndex) {
        return nullptr;
    }
    ++nextViewportIndex_;

    std::unique_ptr<ViewportSession> session = std::make_unique<ViewportSession>();
    session->host = host;
    session->key = key;
    session->viewportIndex = viewportIndex;
    sessions_.push_back(std::move(session));
    return sessions_.back().get();
}

EditorSceneBgfxViewport::ViewportSession* EditorSceneBgfxViewport::FindSession(HWND host, std::uint64_t key) noexcept {
    if (host == nullptr) {
        return nullptr;
    }

    const auto iter = std::ranges::find_if(sessions_, [host, key](const std::unique_ptr<ViewportSession>& session) {
        return session != nullptr && session->host == host && session->key == key;
    });
    return iter == sessions_.end() ? nullptr : iter->get();
}

EditorSceneBgfxViewport::ViewportSession* EditorSceneBgfxViewport::FindSessionByKey(std::uint64_t key) noexcept {
    if (key == 0U) {
        return nullptr;
    }

    const auto iter = std::ranges::find_if(sessions_, [key](const std::unique_ptr<ViewportSession>& session) {
        return session != nullptr && session->key == key;
    });
    return iter == sessions_.end() ? nullptr : iter->get();
}

EditorSceneBgfxViewport::HostSurface* EditorSceneBgfxViewport::EnsureHostSurface(HWND host) {
    if (host == nullptr) {
        return nullptr;
    }
    if (HostSurface* existing = FindHostSurface(host); existing != nullptr) {
        return existing;
    }

    std::unique_ptr<HostSurface> surface = std::make_unique<HostSurface>();
    surface->host = host;
    hostSurfaces_.push_back(std::move(surface));
    return hostSurfaces_.back().get();
}

EditorSceneBgfxViewport::HostSurface* EditorSceneBgfxViewport::FindHostSurface(HWND host) noexcept {
    if (host == nullptr) {
        return nullptr;
    }

    const auto iter = std::ranges::find_if(hostSurfaces_, [host](const std::unique_ptr<HostSurface>& surface) {
        return surface != nullptr && surface->host == host;
    });
    return iter == hostSurfaces_.end() ? nullptr : iter->get();
}

EditorSceneBgfxViewport::HostSurface* EditorSceneBgfxViewport::FindHostSurfaceByWindow(HWND window) noexcept {
    if (window == nullptr) {
        return nullptr;
    }

    const auto iter = std::ranges::find_if(hostSurfaces_, [window](const std::unique_ptr<HostSurface>& surface) {
        return surface != nullptr && surface->window == window;
    });
    return iter == hostSurfaces_.end() ? nullptr : iter->get();
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

bool EditorSceneBgfxViewport::EnsureHostSurfaceWindow(HostSurface& surface, const RECT& rect, std::span<const PendingPresent*> presents) {
    if (surface.host == nullptr || RectWidth(rect) == 0U || RectHeight(rect) == 0U || !EnsureWindowClass()) {
        return false;
    }

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
    } else if (GetParent(surface.window) != surface.host) {
        ShowWindow(surface.window, SW_HIDE);
        SetParent(surface.window, surface.host);
    }

    SetWindowPos(
        surface.window,
        HWND_TOP,
        rect.left,
        rect.top,
        static_cast<int>(RectWidth(rect)),
        static_cast<int>(RectHeight(rect)),
        SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOREDRAW);

    HRGN combinedRegion = CreateRectRgn(0, 0, 0, 0);
    if (combinedRegion == nullptr) {
        return false;
    }
    for (const PendingPresent* present : presents) {
        if (present == nullptr) {
            continue;
        }
        const int left = static_cast<int>(present->destination.left - rect.left);
        const int top = static_cast<int>(present->destination.top - rect.top);
        const int right = static_cast<int>(present->destination.right - rect.left);
        const int bottom = static_cast<int>(present->destination.bottom - rect.top);
        HRGN presentRegion = CreateRectRgn(left, top, right, bottom);
        if (presentRegion == nullptr) {
            DeleteObject(combinedRegion);
            return false;
        }
        CombineRgn(combinedRegion, combinedRegion, presentRegion, RGN_OR);
        DeleteObject(presentRegion);
    }
    if (SetWindowRgn(surface.window, combinedRegion, FALSE) == 0) {
        DeleteObject(combinedRegion);
        return false;
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
    config.syncMode = render::DisplaySyncMode::VSync;
    config.targetFps = 120;
    config.flushAfterRender = true;
    bgfx::RendererType::Enum supportedBackends[bgfx::RendererType::Count]{};
    const std::uint8_t supportedBackendCount = bgfx::getSupportedRenderers(static_cast<std::uint8_t>(bgfx::RendererType::Count), supportedBackends);
    const bgfx::RendererType::Enum preferredBackend = EditorBgfxBackendSelector::Resolve(supportedBackends, supportedBackendCount, backendSettings_);
    config.preferredBgfxRendererType = preferredBackend == bgfx::RendererType::Count ? -1 : static_cast<std::int32_t>(preferredBackend);

    if (!renderer_.Initialize(surface, &config)) {
        return false;
    }

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
    if (surface.window != nullptr && IsWindow(surface.window) != 0) {
        ShowWindow(surface.window, SW_HIDE);
    }
    surface.presentedInCurrentPaint = false;
}

void EditorSceneBgfxViewport::HideSession(HWND host, std::uint64_t key) noexcept {
    ViewportSession* session = FindSession(host, key);
    if (session != nullptr) {
        session->presentedInCurrentPaint = false;
        if (HostSurface* surface = FindHostSurface(session->host); surface != nullptr) {
            HideHostSurface(*surface);
        }
    }
}

void EditorSceneBgfxViewport::ReleaseWindow(HWND window) noexcept {
    if (window == contextWindow_) {
        contextWindow_ = nullptr;
        return;
    }

    HostSurface* surface = FindHostSurfaceByWindow(window);
    if (surface != nullptr) {
        surface->presentTarget.Shutdown();
        surface->window = nullptr;
        surface->rect = {};
        surface->presentedInCurrentPaint = false;
    }
}

void EditorSceneBgfxViewport::ShutdownGpuResources() noexcept {
    ShutdownSessionFramebuffers();
    renderer_.Shutdown();
    for (const std::unique_ptr<HostSurface>& surface : hostSurfaces_) {
        if (surface != nullptr) {
            if (surface->window != nullptr && IsWindow(surface->window) != 0) {
                const HWND window = surface->window;
                surface->window = nullptr;
                DestroyWindow(window);
            } else {
                surface->window = nullptr;
            }
            surface->rect = {};
            surface->presentedInCurrentPaint = false;
        }
    }
    if (contextWindow_ != nullptr && IsWindow(contextWindow_) != 0) {
        const HWND window = contextWindow_;
        contextWindow_ = nullptr;
        DestroyWindow(window);
    } else {
        contextWindow_ = nullptr;
    }
}

void EditorSceneBgfxViewport::ShutdownSessionFramebuffers() noexcept {
    for (const std::unique_ptr<ViewportSession>& session : sessions_) {
        if (session != nullptr) {
            session->sceneTarget.Shutdown();
            session->postProcessTargets.Shutdown();
        }
    }
    for (const std::unique_ptr<HostSurface>& surface : hostSurfaces_) {
        if (surface != nullptr) {
            surface->presentTarget.Shutdown();
        }
    }
}

bool EditorSceneBgfxViewport::SubmitPendingPaint() {
    pendingSubmissions_.clear();
    if (pendingPresents_.empty()) {
        return true;
    }

    for (PendingPresent& present : pendingPresents_) {
        if (present.host == nullptr) {
            continue;
        }

        std::vector<const PendingPresent*> hostPresents;
        RECT surfaceRect = present.destination;
        for (const PendingPresent& candidate : pendingPresents_) {
            if (candidate.host != present.host) {
                continue;
            }
            hostPresents.push_back(&candidate);
            UnionRect(&surfaceRect, &surfaceRect, &candidate.destination);
        }

        HostSurface* surface = EnsureHostSurface(present.host);
        if (surface == nullptr) {
            return false;
        }
        if (surface->presentedInCurrentPaint) {
            continue;
        }
        if (!EnsureHostSurfaceWindow(*surface, surfaceRect, hostPresents)) {
            return false;
        }
        if (!EnsurePresentTarget(*surface, RectWidth(surfaceRect), RectHeight(surfaceRect))) {
            return false;
        }
        surface->presentedInCurrentPaint = true;

        for (const PendingPresent* hostPresent : hostPresents) {
            if (hostPresent == nullptr) {
                continue;
            }
            render::Renderer::SceneFrameSubmission submission{};
            if (!BuildSubmission(*hostPresent, *surface, submission)) {
                return false;
            }
            pendingSubmissions_.push_back(submission);
        }
    }

    if (pendingSubmissions_.empty()) {
        return true;
    }
    if (!renderer_.BeginFrame()) {
        return false;
    }

    const bool submitted = renderer_.SubmitScenes(pendingSubmissions_);
    renderer_.EndFrame();
    if (!submitted) {
        return false;
    }

    for (const std::unique_ptr<HostSurface>& surface : hostSurfaces_) {
        if (surface != nullptr && surface->presentedInCurrentPaint && surface->window != nullptr && IsWindow(surface->window) != 0) {
            SetWindowPos(
                surface->window,
                HWND_TOP,
                surface->rect.left,
                surface->rect.top,
                static_cast<int>(RectWidth(surface->rect)),
                static_cast<int>(RectHeight(surface->rect)),
                SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOREDRAW | SWP_SHOWWINDOW);
        }
    }
    return true;
}

void EditorSceneBgfxViewport::FailRender(const char* reason) noexcept {
    MessageBoxA(nullptr, reason == nullptr ? "Scene render failed" : reason, "21kb Editor - Scene Render Fatal", MB_OK | MB_ICONERROR);
    std::abort();
}

bool EditorSceneBgfxViewport::RenderAndPresent(HDC dc, const RECT& rect, ViewportSession& session, const kb::scene::Scene& scene, const PresentSettings& settings) {
    static_cast<void>(dc);
    return QueuePresent(rect, session, scene, settings);
}

bool EditorSceneBgfxViewport::QueuePresent(const RECT& rect, ViewportSession& session, const kb::scene::Scene& scene, const PresentSettings& settings) {
    const RECT clippedPanel = ClipRectToClient(session.host, rect);
    const std::uint32_t panelWidth = RectWidth(clippedPanel);
    const std::uint32_t panelHeight = RectHeight(clippedPanel);
    if (panelWidth == 0U || panelHeight == 0U) {
        HideSession(session.host, session.key);
        return true;
    }
    const std::uint32_t width = settings.renderWidth == 0U ? panelWidth : settings.renderWidth;
    const std::uint32_t height = settings.renderHeight == 0U ? panelHeight : settings.renderHeight;
    const RECT destination = ClipRectToClient(session.host, CenteredRectFor(clippedPanel, width, height, settings.fitMode));
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

bool EditorSceneBgfxViewport::BuildSubmission(const PendingPresent& present, const HostSurface& surface, render::Renderer::SceneFrameSubmission& submission) {
    if (present.session == nullptr || present.scene == nullptr || present.renderWidth == 0U || present.renderHeight == 0U ||
        present.outputWidth == 0U || present.outputHeight == 0U || !surface.presentTarget.IsValid()) {
        return false;
    }

    ViewportSession& session = *present.session;
    if (!session.sceneTarget.Ensure(render::SceneRenderTargetDesc{
            .extent = render::RenderExtent{present.renderWidth, present.renderHeight},
            .colorPolicy = render::SceneColorFormatPolicy::Auto,
        })) {
        return false;
    }
    if (!session.postProcessTargets.Ensure(render::ScenePostProcessTargetsDesc{
            .extent = render::RenderExtent{present.renderWidth, present.renderHeight},
            .colorPolicy = render::SceneColorFormatPolicy::Auto,
        })) {
        return false;
    }

    session.selectedEntityIds = present.settings.selectedEntityIds;
    const render::RenderViewportRect outputRect{
        .x = static_cast<std::uint32_t>(std::max<LONG>(0, present.destination.left - surface.rect.left)),
        .y = static_cast<std::uint32_t>(std::max<LONG>(0, present.destination.top - surface.rect.top)),
        .extent = render::RenderExtent{present.outputWidth, present.outputHeight},
    };
    submission = render::Renderer::SceneFrameSubmission{
        .scene = present.scene,
        .desc = render::RenderSceneSubmitDesc{
        .target = render::RenderSceneTargetBinding{
            .frameBuffer = session.sceneTarget.FrameBuffer(),
            .colorTexture = session.sceneTarget.ColorTexture(),
            .depthTexture = session.sceneTarget.DepthTexture(),
            .viewport = render::RenderViewportDesc{
                .id = render::RenderViewportId{ session.viewportIndex + 1U },
                .extent = render::RenderExtent{ present.renderWidth, present.renderHeight },
                .viewportIndex = session.viewportIndex,
            },
        },
        .postProcess = session.postProcessTargets.Binding(),
        .finalComposite = render::RenderFinalCompositeTargetBinding{
            .frameBuffer = surface.presentTarget.FrameBuffer(),
            .extent = render::RenderExtent{ present.outputWidth, present.outputHeight },
            .outputRect = outputRect,
            .enabled = true,
            .clearTarget = true,
        },
        .cameraOverride = present.settings.cameraOverride,
        .selectedEntityIds = session.selectedEntityIds[0] == 0U
            ? std::span<const std::uint64_t>{}
            : std::span<const std::uint64_t>{session.selectedEntityIds.data(), session.selectedEntityIds.size()},
        .clearRgba = kSceneClearRgba,
        .editorSceneOverlaysEnabled = present.settings.editorSceneOverlaysEnabled,
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
