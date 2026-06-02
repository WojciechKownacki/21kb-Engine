#include "rendering/EditorSceneBgfxViewport.hpp"

#if defined(_WIN32)
#include "engine/scene/Scene.hpp"
#include "kb/render/ViewIdPolicy.hpp"

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include <algorithm>
#include <cstdint>
#include <d2d1helper.h>
#include <limits>
#include <cmath>
#include <vector>

namespace kb::editor {
namespace {

constexpr wchar_t kSceneViewportClassName[] = L"KBEditorSceneBgfxViewport";
constexpr std::uint32_t kSceneClearRgba = 0x000000FFU;
constexpr std::uint32_t kMaxEditorViewportIndex =
    (render::ViewId::Max - render::ViewId::DetachedViewportStart) / render::ViewId::DetachedViewportStride;

template <typename T>
void SafeRelease(T*& value) noexcept {
    if (value != nullptr) {
        value->Release();
        value = nullptr;
    }
}

[[nodiscard]] std::uint32_t RectWidth(const RECT& rect) noexcept {
    return static_cast<std::uint32_t>(std::max<LONG>(0, rect.right - rect.left));
}

[[nodiscard]] std::uint32_t RectHeight(const RECT& rect) noexcept {
    return static_cast<std::uint32_t>(std::max<LONG>(0, rect.bottom - rect.top));
}

[[nodiscard]] bool FitsBgfxTextureExtent(std::uint32_t value) noexcept {
    return value > 0U && value <= static_cast<std::uint32_t>(std::numeric_limits<std::uint16_t>::max());
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

EditorSceneBgfxViewport::SceneTexturePresentTarget::~SceneTexturePresentTarget() {
    Shutdown();
}

bool EditorSceneBgfxViewport::SceneTexturePresentTarget::Ensure(std::uint32_t width, std::uint32_t height) {
    if (!FitsBgfxTextureExtent(width) || !FitsBgfxTextureExtent(height)) {
        Shutdown();
        return false;
    }

    if (bgfx::isValid(frameBuffer_) && texture_ != nullptr && surface_ != nullptr && width_ == width && height_ == height) {
        return true;
    }

    Shutdown();

    if (bgfx::getRendererType() != bgfx::RendererType::Direct3D11) {
        return false;
    }

    const bgfx::InternalData* internalData = bgfx::getInternalData();
    if (internalData == nullptr || internalData->context == nullptr) {
        return false;
    }

    auto* device = static_cast<ID3D11Device*>(internalData->context);
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    ID3D11Texture2D* texture = nullptr;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture)) || texture == nullptr) {
        return false;
    }

    IDXGISurface* surface = nullptr;
    if (FAILED(texture->QueryInterface(__uuidof(IDXGISurface), reinterpret_cast<void**>(&surface))) || surface == nullptr) {
        SafeRelease(texture);
        return false;
    }

    constexpr std::uint64_t textureFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    bgfxTexture_ = bgfx::createTexture2D(
        static_cast<std::uint16_t>(width),
        static_cast<std::uint16_t>(height),
        false,
        1,
        bgfx::TextureFormat::BGRA8,
        textureFlags);
    if (!bgfx::isValid(bgfxTexture_)) {
        SafeRelease(surface);
        SafeRelease(texture);
        return false;
    }

    (void)bgfx::overrideInternal(bgfxTexture_, reinterpret_cast<std::uintptr_t>(texture));

    frameBuffer_ = bgfx::createFrameBuffer(1, &bgfxTexture_, false);
    if (!bgfx::isValid(frameBuffer_)) {
        bgfx::destroy(bgfxTexture_);
        bgfxTexture_ = BGFX_INVALID_HANDLE;
        SafeRelease(surface);
        SafeRelease(texture);
        return false;
    }

    texture_ = texture;
    surface_ = surface;
    width_ = width;
    height_ = height;
    return true;
}

void EditorSceneBgfxViewport::SceneTexturePresentTarget::Shutdown() noexcept {
    if (bgfx::isValid(frameBuffer_)) {
        bgfx::destroy(frameBuffer_);
        frameBuffer_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(bgfxTexture_)) {
        bgfx::destroy(bgfxTexture_);
        bgfxTexture_ = BGFX_INVALID_HANDLE;
    }
    SafeRelease(surface_);
    SafeRelease(texture_);
    width_ = 0;
    height_ = 0;
}

bgfx::FrameBufferHandle EditorSceneBgfxViewport::SceneTexturePresentTarget::FrameBuffer() const noexcept {
    return frameBuffer_;
}

IDXGISurface* EditorSceneBgfxViewport::SceneTexturePresentTarget::Surface() const noexcept {
    return surface_;
}

std::uint32_t EditorSceneBgfxViewport::SceneTexturePresentTarget::Width() const noexcept {
    return width_;
}

std::uint32_t EditorSceneBgfxViewport::SceneTexturePresentTarget::Height() const noexcept {
    return height_;
}

EditorSceneBgfxViewport::D2DScenePresenter::~D2DScenePresenter() {
    Shutdown();
}

bool EditorSceneBgfxViewport::D2DScenePresenter::EnsureRenderTarget() {
    if (renderTarget_ != nullptr) {
        return true;
    }

    if (factory_ == nullptr) {
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_)) || factory_ == nullptr) {
            return false;
        }
    }

    const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        0.0F,
        0.0F,
        D2D1_RENDER_TARGET_USAGE_NONE,
        D2D1_FEATURE_LEVEL_DEFAULT);

    return SUCCEEDED(factory_->CreateDCRenderTarget(&properties, &renderTarget_)) && renderTarget_ != nullptr;
}

void EditorSceneBgfxViewport::D2DScenePresenter::ResetBitmap() noexcept {
    SafeRelease(bitmap_);
    SafeRelease(bitmapSurface_);
}

bool EditorSceneBgfxViewport::D2DScenePresenter::Present(HDC dc, const RECT& bounds, const RECT& destination, IDXGISurface* surface) {
    if (dc == nullptr || surface == nullptr || RectWidth(bounds) == 0U || RectHeight(bounds) == 0U || RectWidth(destination) == 0U || RectHeight(destination) == 0U) {
        return false;
    }

    if (!EnsureRenderTarget()) {
        return false;
    }

    if (bitmap_ == nullptr || bitmapSurface_ != surface) {
        ResetBitmap();
        const D2D1_BITMAP_PROPERTIES properties = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        if (FAILED(renderTarget_->CreateSharedBitmap(__uuidof(IDXGISurface), surface, &properties, &bitmap_)) || bitmap_ == nullptr) {
            return false;
        }
        bitmapSurface_ = surface;
        bitmapSurface_->AddRef();
    }

    if (FAILED(renderTarget_->BindDC(dc, &bounds))) {
        ResetBitmap();
        return false;
    }

    const float left = static_cast<float>(destination.left - bounds.left);
    const float top = static_cast<float>(destination.top - bounds.top);
    const float right = static_cast<float>(destination.right - bounds.left);
    const float bottom = static_cast<float>(destination.bottom - bounds.top);
    renderTarget_->BeginDraw();
    renderTarget_->DrawBitmap(bitmap_, D2D1::RectF(left, top, right, bottom), 1.0F, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    if (FAILED(renderTarget_->EndDraw())) {
        ResetBitmap();
        return false;
    }

    return true;
}

void EditorSceneBgfxViewport::D2DScenePresenter::Shutdown() noexcept {
    ResetBitmap();
    SafeRelease(renderTarget_);
    SafeRelease(factory_);
}

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

    if (!EnsureRenderer()) {
        HideSession(parent);
        return;
    }

    if (!RenderAndPresent(dc, rect, *session, scene, settings)) {
        HideSession(parent);
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

bool EditorSceneBgfxViewport::EnsureDeviceWindow() {
    if (deviceWindow_ != nullptr && IsWindow(deviceWindow_) != 0) {
        return true;
    }

    if (instance_ == nullptr) {
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
        WS_POPUP | WS_DISABLED | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0,
        0,
        1,
        1,
        nullptr,
        nullptr,
        instance_,
        this);
    return deviceWindow_ != nullptr;
}

bool EditorSceneBgfxViewport::EnsureRenderer() {
    if (renderer_.IsInitialized()) {
        return true;
    }

    if (!EnsureDeviceWindow()) {
        return false;
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
}

void EditorSceneBgfxViewport::ReleaseWindow(HWND window) noexcept {
    if (window == deviceWindow_) {
        for (const std::unique_ptr<ViewportSession>& session : sessions_) {
            if (session != nullptr) {
                session->sceneTarget.Shutdown();
                session->postProcessTargets.Shutdown();
                session->presentTarget.Shutdown();
            }
        }
    }
}

void EditorSceneBgfxViewport::ShutdownGpuResources() noexcept {
    ShutdownSessionFramebuffers();
    scenePresenter_.Shutdown();
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
            session->presentTarget.Shutdown();
        }
    }
}

bool EditorSceneBgfxViewport::RenderAndPresent(HDC dc, const RECT& rect, ViewportSession& session, const kb::scene::Scene& scene, const PresentSettings& settings) {
    if (!renderer_.IsInitialized()) {
        return false;
    }

    const std::uint32_t panelWidth = RectWidth(rect);
    const std::uint32_t panelHeight = RectHeight(rect);
    const std::uint32_t width = settings.renderWidth == 0U ? panelWidth : settings.renderWidth;
    const std::uint32_t height = settings.renderHeight == 0U ? panelHeight : settings.renderHeight;
    render::Renderer::SceneFrameSubmission submission{};
    if (!BuildSubmission(session, scene, width, height, settings, submission)) {
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

    const RECT destination = CenteredRectFor(rect, width, height, settings.fitMode);
    return scenePresenter_.Present(dc, rect, destination, session.presentTarget.Surface());
}

bool EditorSceneBgfxViewport::BuildSubmission(ViewportSession& session, const kb::scene::Scene& scene, std::uint32_t width, std::uint32_t height, const PresentSettings& settings, render::Renderer::SceneFrameSubmission& submission) {
    if (width == 0U || height == 0U) {
        return false;
    }

    if (!session.sceneTarget.Ensure(render::SceneRenderTargetDesc{
            .extent = render::RenderExtent{width, height},
            .colorPolicy = render::SceneColorFormatPolicy::Auto,
        })) {
        return false;
    }
    if (!session.postProcessTargets.Ensure(render::ScenePostProcessTargetsDesc{
            .extent = render::RenderExtent{width, height},
            .colorPolicy = render::SceneColorFormatPolicy::Auto,
        })) {
        return false;
    }

    if (!session.presentTarget.Ensure(width, height)) {
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
                .extent = render::RenderExtent{ width, height },
                .viewportIndex = session.viewportIndex,
            },
        },
        .postProcess = session.postProcessTargets.Binding(),
        .finalComposite = render::RenderFinalCompositeTargetBinding{
            .frameBuffer = session.presentTarget.FrameBuffer(),
            .extent = render::RenderExtent{ width, height },
            .enabled = true,
        },
        .cameraOverride = settings.cameraOverride,
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
