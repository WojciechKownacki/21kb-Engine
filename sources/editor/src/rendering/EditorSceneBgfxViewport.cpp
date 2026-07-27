#include "rendering/EditorSceneBgfxViewport.hpp"

#if defined(_WIN32)
#include "engine/scene/Scene.hpp"
#include "kb/render/ViewIdPolicy.hpp"
#include "rendering/EditorBgfxBackendSelector.hpp"
#include "rendering/MaterialPreviewViewportKeys.hpp"
#include "rendering/EditorSceneViewportGeometry.hpp"
#include "rendering/EditorSceneViewportRegionBuilder.hpp"

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <ranges>
#include <string>
#include <span>
#include <vector>

namespace kb::editor {
namespace {

constexpr wchar_t kSceneViewportClassName[] = L"KBEditorSceneBgfxViewport";
constexpr std::uint32_t kSceneClearRgba = 0x000000FFU;
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

[[nodiscard]] RECT WindowRectInHostClient(HWND window, HWND host) noexcept {
    if (window == nullptr || host == nullptr || IsWindow(window) == 0 || IsWindow(host) == 0) {
        return {};
    }

    RECT rect{};
    if (GetWindowRect(window, &rect) == 0) {
        return {};
    }

    POINT points[2]{
        POINT{rect.left, rect.top},
        POINT{rect.right, rect.bottom},
    };
    static_cast<void>(MapWindowPoints(nullptr, host, points, 2U));

    return RECT{
        .left = points[0].x,
        .top = points[0].y,
        .right = points[1].x,
        .bottom = points[1].y,
    };
}

void EnsureParentChildClipping(HWND parent) noexcept {
    if (parent == nullptr || IsWindow(parent) == 0) {
        return;
    }

    const LONG_PTR style = GetWindowLongPtrW(parent, GWL_STYLE);
    if (style == 0) {
        return;
    }

    constexpr LONG_PTR requiredStyle = WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    if ((style & requiredStyle) == requiredStyle) {
        return;
    }

    SetWindowLongPtrW(parent, GWL_STYLE, style | requiredStyle);
    SetWindowPos(parent, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
}

[[nodiscard]] const char* DiagnosticKindLabel(render::SceneRenderDiagnosticKind kind) noexcept {
    switch (kind) {
    case render::SceneRenderDiagnosticKind::MissingMeshBinding:
        return "missing mesh binding";
    case render::SceneRenderDiagnosticKind::MissingMeshResource:
        return "missing mesh resource";
    case render::SceneRenderDiagnosticKind::UnsupportedMeshVertexFormat:
        return "unsupported mesh vertex format";
    case render::SceneRenderDiagnosticKind::MissingMaterialBinding:
        return "missing material binding";
    case render::SceneRenderDiagnosticKind::MissingMaterialResource:
        return "missing material resource";
    case render::SceneRenderDiagnosticKind::MissingTextureBinding:
        return "missing texture binding";
    case render::SceneRenderDiagnosticKind::MissingTextureResource:
        return "missing texture resource";
    case render::SceneRenderDiagnosticKind::TextureDimensionMismatch:
        return "texture dimension mismatch";
    case render::SceneRenderDiagnosticKind::UnresolvedMaterialTexturePath:
        return "unresolved material texture path";
    case render::SceneRenderDiagnosticKind::MissingMaterialAsset:
        return "missing material asset";
    case render::SceneRenderDiagnosticKind::InvalidMaterialAsset:
        return "invalid material asset";
    case render::SceneRenderDiagnosticKind::UnsupportedMaterialAlphaBlend:
        return "alpha blend material disabled until transparent pass is ready";
    case render::SceneRenderDiagnosticKind::DroppedInstances:
        return "dropped instances";
    case render::SceneRenderDiagnosticKind::GraphMaterialProgramFallback:
        return "graph material program fallback";
    case render::SceneRenderDiagnosticKind::GraphMaterialProgramUnavailable:
        return "graph material program unavailable";
    case render::SceneRenderDiagnosticKind::DeferredRendererUnavailable:
        return "deferred renderer unavailable";
    }
    return "unknown render diagnostic";
}

[[nodiscard]] std::string RendererFailureDetail(const render::Renderer& renderer) {
    const render::SceneRenderDiagnostics& diagnostics = renderer.LastSceneDiagnostics();
    if (diagnostics.events.empty()) {
        return "Renderer submission failed without scene diagnostics.";
    }

    const render::SceneRenderDiagnosticEvent& event = diagnostics.events.front();
    std::string detail = std::string{"Renderer diagnostic: "} + DiagnosticKindLabel(event.kind);
    if (event.entityId != 0U) {
        detail += " entity=" + std::to_string(event.entityId);
    }
    if (event.meshAssetId != 0U) {
        detail += " meshAsset=" + std::to_string(event.meshAssetId);
    }
    if (event.materialAssetId != 0U) {
        detail += " materialAsset=" + std::to_string(event.materialAssetId);
    }
    if (event.textureAssetId != 0U) {
        detail += " textureAsset=" + std::to_string(event.textureAssetId);
    }
    if (event.kind == render::SceneRenderDiagnosticKind::TextureDimensionMismatch) {
        detail += " expected=" + std::string{ render::RenderTextureDimensionName(event.expectedTextureDimension) };
        detail += " actual=" + std::string{ render::RenderTextureDimensionName(event.actualTextureDimension) };
        detail += " fallback=" + std::string{ render::RenderTextureDimensionName(event.fallbackTextureDimension) };
    }
    if (event.instanceCount != 0U) {
        detail += " instances=" + std::to_string(event.instanceCount);
    }
    return detail;
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
    EnsureParentChildClipping(parent);
}

void EditorSceneBgfxViewport::SetErrorReporter(std::function<void(std::string_view)> reporter) noexcept {
    errorReporter_ = std::move(reporter);
}

void EditorSceneBgfxViewport::SetAaTraceReporter(std::function<void(std::string_view)> reporter) noexcept {
    aaTraceReporter_ = std::move(reporter);
}

void EditorSceneBgfxViewport::SetGraphShaderCacheRoot(std::string root) {
    if (graphShaderCacheRoot_ == root) {
        return;
    }
    graphShaderCacheRoot_ = std::move(root);
    if (renderer_.IsInitialized()) {
        renderer_.SetGraphShaderCacheRoot(graphShaderCacheRoot_);
    }
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

    TrackPaintHost(parent);
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
            RequestPresent();
        }

        if (surface->window != nullptr && IsWindow(surface->window) != 0) {
            static_cast<void>(EnsureHostSurfaceWindow(*surface, layoutBounds));
        }

        if (surface->clipWindow != nullptr && IsWindow(surface->clipWindow) != 0 && IsWindowVisible(surface->clipWindow) == 0) {
            RequestPresent();
        }
        hostSurfaceStore_.MarkLayoutActive(*surface);
    }

    if (hostSurfaceStore_.HasVisibleUnpresentedForHost(parent)) {
        RequestPresent();
    }
}

void EditorSceneBgfxViewport::SyncHostSurfaceLayoutsForResize(HWND parent, std::span<const HostSurfaceLayout> layouts) noexcept {
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

        surface->layoutBounds = layoutBounds;
        surface->hasLayoutBounds = true;
        if (surface->window != nullptr && IsWindow(surface->window) != 0) {
            static_cast<void>(EnsureHostSurfaceWindow(*surface, layoutBounds));
        }
        hostSurfaceStore_.MarkLayoutActive(*surface);
    }

    hostSurfaceStore_.HideUnpresentedForHost(parent);
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
    paintHosts_.clear();
    contextWindow_ = nullptr;
    backendSettings_ = nullptr;
    rendererBackendGeneration_ = 0;
    pendingPresents_.clear();
    pendingSubmissions_.clear();
    presentRequested_ = true;
    renderFailed_ = false;
    renderFailureReported_ = false;
    failureDetail_.clear();
    lastConsoleAaTrace_.clear();
}

void EditorSceneBgfxViewport::BeginPaintLayout() noexcept {
    BeginPaintLayout(defaultParent_);
}

void EditorSceneBgfxViewport::BeginPaintLayout(HWND parent) noexcept {
    paintParent_ = parent;
    paintHosts_.clear();
    pendingPresents_.clear();
    pendingSubmissions_.clear();
    failureDetail_.clear();
    TrackPaintHost(parent);
}

void EditorSceneBgfxViewport::ReleaseScene(
    const kb::scene::Scene& scene) noexcept {
    renderer_.ReleaseScene(scene);
}

void EditorSceneBgfxViewport::EndPaintLayout() {
    if (!renderFailed_ && !SubmitPendingPaint()) {
        FailRender("Scene render/present failed during queued viewport submit. The editor will stay open, but the scene viewport was disabled.");
    }

    for (const HWND host : paintHosts_) {
        hostSurfaceStore_.HideUnpresentedForHost(host);
    }
    pendingPresents_.clear();
    pendingSubmissions_.clear();
    paintParent_ = nullptr;
    paintHosts_.clear();
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

bool EditorSceneBgfxViewport::IsHostSurfaceVisible(HWND host, std::uint64_t key) noexcept {
    const HostSurface* surface = FindHostSurface(host, key);
    return surface != nullptr && surface->clipWindow != nullptr && IsWindow(surface->clipWindow) != 0 && IsWindowVisible(surface->clipWindow) != 0;
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

bool EditorSceneBgfxViewport::EnsureHostSurfaceWindow(HostSurface& surface, const RECT& requestedRect, bool preserveBits) {
    if (surface.host == nullptr || RectWidth(requestedRect) == 0U || RectHeight(requestedRect) == 0U || !EnsureWindowClass()) {
        return false;
    }

    // The native child window and its swapchain-backed present target must always agree on size --
    // holding one back while the other tracks the live panel rect showed up as either duplicated /
    // stretched content (window ahead of swapchain) or a black flash (swapchain ahead of window, or a
    // premature present with nothing submitted yet). So this always resizes to the live requested
    // rect immediately; see EnsurePresentTarget for why recreation itself is now cheap enough to do
    // on every call without a throttle.
    const RECT rect = requestedRect;

    EnsureParentChildClipping(surface.host);
    preserveBits = preserveBits || ShouldPreserveHostSurfaceBits(surface.key);
    const RECT actualRect = WindowRectInHostClient(surface.clipWindow, surface.host);
    bool needsPositionUpdate = !RectEquals(surface.rect, rect) || !RectEquals(actualRect, rect);
    bool needsRegionUpdate = needsPositionUpdate;

    if (surface.clipWindow == nullptr || IsWindow(surface.clipWindow) == 0) {
        surface.clipWindow = CreateWindowExW(
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
        if (surface.clipWindow == nullptr) {
            return false;
        }
        needsPositionUpdate = false;
        needsRegionUpdate = true;
    } else if (GetParent(surface.clipWindow) != surface.host) {
        ShowWindow(surface.clipWindow, SW_HIDE);
        SetParent(surface.clipWindow, surface.host);
        needsPositionUpdate = true;
        needsRegionUpdate = true;
    }

    if (surface.window == nullptr || IsWindow(surface.window) == 0) {
        surface.window = CreateWindowExW(
            0,
            kSceneViewportClassName,
            L"",
            WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
            0,
            0,
            static_cast<int>(RectWidth(rect)),
            static_cast<int>(RectHeight(rect)),
            surface.clipWindow,
            nullptr,
            instance_,
            this);
        if (surface.window == nullptr) {
            return false;
        }
        needsRegionUpdate = true;
    } else if (GetParent(surface.window) != surface.clipWindow) {
        ShowWindow(surface.window, SW_HIDE);
        SetParent(surface.window, surface.clipWindow);
        needsPositionUpdate = true;
        needsRegionUpdate = true;
    }

    if (needsPositionUpdate) {
        UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER;
        if (!preserveBits) {
            flags |= SWP_NOCOPYBITS;
        }
        if (SetWindowPos(
            surface.clipWindow,
            HWND_BOTTOM,
            rect.left,
            rect.top,
            static_cast<int>(RectWidth(rect)),
            static_cast<int>(RectHeight(rect)),
            flags) == 0) {
            return false;
        }

        const RECT movedRect = WindowRectInHostClient(surface.clipWindow, surface.host);
        if (!RectEquals(movedRect, rect)) {
            if (SetWindowPos(
                surface.clipWindow,
                HWND_BOTTOM,
                rect.left,
                rect.top,
                static_cast<int>(RectWidth(rect)),
                static_cast<int>(RectHeight(rect)),
                flags) == 0) {
                return false;
            }
            const RECT retryRect = WindowRectInHostClient(surface.clipWindow, surface.host);
            if (!RectEquals(retryRect, rect)) {
                return false;
            }
        }

        if (SetWindowPos(
            surface.window,
            HWND_TOP,
            0,
            0,
            static_cast<int>(RectWidth(rect)),
            static_cast<int>(RectHeight(rect)),
            flags) == 0) {
            return false;
        }
    }

    if (needsRegionUpdate) {
        const RECT windowRegionRect{0, 0, static_cast<LONG>(RectWidth(rect)), static_cast<LONG>(RectHeight(rect))};
        HRGN clipRegion = CreateRectRgn(windowRegionRect.left, windowRegionRect.top, windowRegionRect.right, windowRegionRect.bottom);
        if (clipRegion == nullptr) {
            return false;
        }
        if (SetWindowRgn(surface.clipWindow, clipRegion, FALSE) == 0) {
            DeleteObject(clipRegion);
            return false;
        }

        HRGN renderRegion = preserveBits
            ? CreateRectRgn(windowRegionRect.left, windowRegionRect.top, windowRegionRect.right, windowRegionRect.bottom)
            : EditorSceneViewportRegionBuilder::BuildCombinedRegion(windowRegionRect, std::span<const RECT>{&windowRegionRect, 1U});
        if (renderRegion == nullptr) {
            return false;
        }
        if (SetWindowRgn(surface.window, renderRegion, FALSE) == 0) {
            DeleteObject(renderRegion);
            return false;
        }
    }

    surface.rect = rect;
    return true;
}

bool EditorSceneBgfxViewport::EnsureRenderer() {
    bool hasPreservedExposure = false;
    float preservedExposureLuminance = 0.0F;
    if (renderer_.IsInitialized()) {
        const std::uint64_t requestedGeneration = backendSettings_ == nullptr ? 0U : backendSettings_->BackendGeneration();
        if (rendererBackendGeneration_ == requestedGeneration) {
            return true;
        }
        // A generation bump here (MSAA sample-count/mode change, backend switch) forces a full
        // Shutdown()+Initialize() below purely because bgfx needs the device recreated -- it has
        // nothing to do with the scene actually getting brighter or darker. Renderer::Shutdown()
        // resets the auto-exposure meter, and its default metering mode reads back the actually
        // rendered HDR frame; carrying the last adapted luminance across the reinit avoids the
        // image visibly (and incorrectly) re-exposing from a neutral 0.18 baseline every time.
        hasPreservedExposure = renderer_.HasExposureHistory();
        preservedExposureLuminance = renderer_.CurrentExposureLuminance();
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
    config.msaaSamples = backendSettings_ == nullptr ? 0U : backendSettings_->MsaaSamples();
    rendererMsaaSamples_ = config.msaaSamples;
    bgfx::RendererType::Enum supportedBackends[bgfx::RendererType::Count]{};
    const std::uint8_t supportedBackendCount = bgfx::getSupportedRenderers(static_cast<std::uint8_t>(bgfx::RendererType::Count), supportedBackends);
    const bgfx::RendererType::Enum preferredBackend = EditorBgfxBackendSelector::Resolve(supportedBackends, supportedBackendCount, backendSettings_);
    config.preferredBgfxRendererType = preferredBackend == bgfx::RendererType::Count ? -1 : static_cast<std::int32_t>(preferredBackend);

    if (!renderer_.Initialize(surface, &config)) {
        SetFailureDetail("Renderer initialization failed for this viewport. Material preview and scene view cannot use separate bgfx renderer instances in the same process.");
        return false;
    }
    if (hasPreservedExposure) {
        renderer_.PrimeExposureAdaptation(preservedExposureLuminance);
    }

    renderer_.SetRuntimeAssetDiscoveryEnabled(false);
    if (!graphShaderCacheRoot_.empty()) {
        renderer_.SetGraphShaderCacheRoot(graphShaderCacheRoot_);
    }
    rendererBackendGeneration_ = backendSettings_ == nullptr ? 0U : backendSettings_->BackendGeneration();
    return true;
}

bool EditorSceneBgfxViewport::EnsurePresentTarget(HostSurface& surface, std::uint32_t width, std::uint32_t height) {
    if (surface.window == nullptr || !renderer_.IsInitialized()) {
        SetFailureDetail("Present target creation was requested before the viewport renderer or native child surface was ready.");
        return false;
    }

    // flushBeforeRecreate used to force an explicit bgfx::frame() here, outside of and *before* this
    // same paint's own BeginFrame()/EndFrame() pair (see SubmitPreparedSubmissions, called right after
    // this). That stray frame boundary did two things wrong: it was a full synchronous GPU stall on
    // every resize-driven repaint (the original stutter-to-10fps complaint), and it flushed a frame
    // with nothing submitted yet to the just-recreated swapchain, which is what actually painted it
    // black for an instant -- not a GDI erase issue. The destroy() this Ensure() call queues is
    // deferred and gets processed by the *normal* end-of-frame bgfx::frame() a few lines down the
    // call stack regardless, so no extra flush is needed for correctness here.
    if (!surface.presentTarget.Ensure(render::NativeWindowFramebufferDesc{
        .nativeWindow = surface.window,
        .width = width,
        .height = height,
        .colorFormat = bgfx::TextureFormat::BGRA8,
        .depthFormat = bgfx::TextureFormat::Count,
        .flushBeforeRecreate = false,
    })) {
        SetFailureDetail("Native window framebuffer creation failed for the viewport present surface.");
        return false;
    }
    return true;
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
    sessionStore_.ResetSubmittedSceneRevisions();
    hostSurfaceStore_.ShutdownPresentTargets();
}

bool EditorSceneBgfxViewport::SubmitPendingPaint() {
    pendingSubmissions_.clear();
    if (pendingPresents_.empty()) {
        return true;
    }

    // MAT-72: advance the renderer clock by real elapsed time so time-driven graph nodes animate live in
    // the editor (u_time was previously only set by tests, so Panner/Time/Rotator stayed frozen at t=0).
    if (renderer_.IsInitialized()) {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        float deltaSeconds = 0.0F;
        if (hasLastFrameClock_) {
            deltaSeconds = std::chrono::duration<float>(now - lastFrameClock_).count();
            deltaSeconds = std::clamp(deltaSeconds, 0.0F, 0.25F);
        }
        lastFrameClock_ = now;
        hasLastFrameClock_ = true;
        renderer_.SetFrameDeltaSeconds(deltaSeconds);
    }

    PendingPaintSubmitter submitter(*this);
    return submitter.Submit(std::span<const PendingPresent>{pendingPresents_.data(), pendingPresents_.size()});
}

void EditorSceneBgfxViewport::ReportAaTrace(std::string_view message, bool force) {
    if (!aaTraceReporter_) {
        return;
    }
    if (!force && lastConsoleAaTrace_ == message) {
        return;
    }
    lastConsoleAaTrace_ = std::string{ message };
    aaTraceReporter_(lastConsoleAaTrace_);
}

void EditorSceneBgfxViewport::ReportAaRouteTrace(std::string_view message, bool force) {
    if (!aaTraceReporter_) {
        return;
    }
    if (!force && lastConsoleAaRouteTrace_ == message) {
        return;
    }
    lastConsoleAaRouteTrace_ = std::string{ message };
    aaTraceReporter_(lastConsoleAaRouteTrace_);
}

void EditorSceneBgfxViewport::ReportAaPipelineTrace(std::string_view message, bool force) {
    if (!aaTraceReporter_) {
        return;
    }
    if (!force && lastConsoleAaPipelineTrace_ == message) {
        return;
    }
    lastConsoleAaPipelineTrace_ = std::string{ message };
    aaTraceReporter_(lastConsoleAaPipelineTrace_);
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
    std::string message = reason == nullptr ? "Scene render failed. The editor will stay open, but the scene viewport was disabled." : reason;
    if (!failureDetail_.empty()) {
        message += "\n\nDetails: ";
        message += failureDetail_;
    }
    if (errorReporter_) {
        errorReporter_(message);
    }
    MessageBoxA(nullptr, message.c_str(), "21kb Editor - Scene Render Failed", MB_OK | MB_ICONERROR);
}

void EditorSceneBgfxViewport::SetFailureDetail(std::string detail) {
    if (!detail.empty()) {
        failureDetail_ = std::move(detail);
    }
}

void EditorSceneBgfxViewport::TrackPaintHost(HWND parent) noexcept {
    if (parent == nullptr || std::ranges::find(paintHosts_, parent) != paintHosts_.end()) {
        return;
    }
    paintHosts_.push_back(parent);
    sessionStore_.MarkHostNotPresented(parent);
    hostSurfaceStore_.MarkHostNotPresented(parent);
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

bool EditorSceneBgfxViewport::ShouldPreserveHostSurfaceBits(std::uint64_t viewportKey) noexcept {
    // Used to gate material-preview surfaces only; every other surface (the Scene viewport included)
    // got SWP_NOCOPYBITS on resize/reshow plus an explicit WM_ERASEBKGND black fill. That forces a
    // full black frame every single time the surface's native child window actually commits a resize
    // -- harmless when resizes were effectively continuous, but once resize target recreation got
    // throttled (see EnsureHostSurfaceWindow's interactive-resize throttle) each throttled commit now
    // sits behind a visibly isolated black flash instead of blending into constant repaint noise.
    // Preserving bits everywhere means a resize briefly shows the previous frame stretched/cropped
    // instead of black, which is what a GPU-presented viewport should do regardless of surface kind.
    static_cast<void>(viewportKey);
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
        const HostSurface* surface = viewport == nullptr ? nullptr : viewport->hostSurfaceStore_.FindByWindow(window);
        if (surface == nullptr || !ShouldPreserveHostSurfaceBits(surface->key)) {
            RECT client{};
            if (GetClientRect(window, &client) != 0) {
                FillRect(reinterpret_cast<HDC>(wparam), &client, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            }
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
