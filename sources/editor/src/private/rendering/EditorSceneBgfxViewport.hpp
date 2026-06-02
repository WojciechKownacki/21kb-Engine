#pragma once

#include "kb/render/DisplayConfig.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/SceneRenderTarget.hpp"
#include "kb/render/post/ScenePostProcessTargets.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorViewportPreviewState.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d2d1.h>
#include <d3d11.h>
#include <dxgi.h>
#endif

namespace kb::scene {
class Scene;
}

namespace kb::editor {

class EditorSceneBgfxViewport {
public:
#if defined(_WIN32)
    struct PresentSettings {
        std::uint32_t renderWidth = 0;
        std::uint32_t renderHeight = 0;
        EditorViewportFitMode fitMode = EditorViewportFitMode::Fit;
        EditorViewportSafeArea safeArea{};
        std::optional<render::SceneRenderCamera> cameraOverride{};
        bool drawSafeArea = false;
    };

    ~EditorSceneBgfxViewport();

    EditorSceneBgfxViewport(const EditorSceneBgfxViewport&) = delete;
    EditorSceneBgfxViewport& operator=(const EditorSceneBgfxViewport&) = delete;

    EditorSceneBgfxViewport() = default;

    void Configure(HINSTANCE instance, HWND parent) noexcept;
    void Shutdown();
    void BeginPaintLayout() noexcept;
    void BeginPaintLayout(HWND parent) noexcept;
    void EndPaintLayout();
    void Present(HDC dc, const RECT& rect, const kb::scene::Scene& scene, const EditorTheme& theme);
    void Present(HDC dc, HWND parent, const RECT& rect, const kb::scene::Scene& scene, const EditorTheme& theme);
    void Present(HDC dc, const RECT& rect, const kb::scene::Scene& scene, const EditorTheme& theme, const PresentSettings& settings);
    void Present(HDC dc, HWND parent, const RECT& rect, const kb::scene::Scene& scene, const EditorTheme& theme, const PresentSettings& settings);
    void Hide() noexcept;

private:
    class SceneTexturePresentTarget final {
    public:
        ~SceneTexturePresentTarget();

        SceneTexturePresentTarget() = default;
        SceneTexturePresentTarget(const SceneTexturePresentTarget&) = delete;
        SceneTexturePresentTarget& operator=(const SceneTexturePresentTarget&) = delete;

        [[nodiscard]] bool Ensure(std::uint32_t width, std::uint32_t height);
        void Shutdown() noexcept;

        [[nodiscard]] bgfx::FrameBufferHandle FrameBuffer() const noexcept;
        [[nodiscard]] IDXGISurface* Surface() const noexcept;
        [[nodiscard]] std::uint32_t Width() const noexcept;
        [[nodiscard]] std::uint32_t Height() const noexcept;

    private:
        ID3D11Texture2D* texture_ = nullptr;
        IDXGISurface* surface_ = nullptr;
        bgfx::TextureHandle bgfxTexture_ = BGFX_INVALID_HANDLE;
        bgfx::FrameBufferHandle frameBuffer_ = BGFX_INVALID_HANDLE;
        std::uint32_t width_ = 0;
        std::uint32_t height_ = 0;
    };

    class D2DScenePresenter final {
    public:
        ~D2DScenePresenter();

        D2DScenePresenter() = default;
        D2DScenePresenter(const D2DScenePresenter&) = delete;
        D2DScenePresenter& operator=(const D2DScenePresenter&) = delete;

        [[nodiscard]] bool Present(HDC dc, const RECT& bounds, const RECT& destination, IDXGISurface* surface);
        void Shutdown() noexcept;

    private:
        [[nodiscard]] bool EnsureRenderTarget();
        void ResetBitmap() noexcept;

        ID2D1Factory* factory_ = nullptr;
        ID2D1DCRenderTarget* renderTarget_ = nullptr;
        ID2D1Bitmap* bitmap_ = nullptr;
        IDXGISurface* bitmapSurface_ = nullptr;
    };

    struct ViewportSession {
        HWND host = nullptr;
        std::uint32_t viewportIndex = 0;
        bool presentedInCurrentPaint = false;
        render::SceneRenderTarget sceneTarget;
        render::ScenePostProcessTargets postProcessTargets;
        SceneTexturePresentTarget presentTarget;
    };

    class Win32Surface final : public render::RenderSurface {
    public:
        explicit Win32Surface(HWND window) noexcept;

        [[nodiscard]] std::uint32_t Width() const noexcept override;
        [[nodiscard]] std::uint32_t Height() const noexcept override;
        [[nodiscard]] void* NativeWindowHandle() const noexcept override;
        [[nodiscard]] void* NativeDisplayHandle() const noexcept override;

    private:
        HWND window_ = nullptr;
    };

    [[nodiscard]] ViewportSession* EnsureSession(HWND host);
    [[nodiscard]] ViewportSession* FindSession(HWND host) noexcept;
    [[nodiscard]] bool EnsureDeviceWindow();
    [[nodiscard]] bool EnsureRenderer();
    void HideSession(HWND host) noexcept;
    void ReleaseWindow(HWND window) noexcept;
    void ShutdownGpuResources() noexcept;
    void ShutdownSessionFramebuffers() noexcept;
    [[nodiscard]] bool RenderAndPresent(HDC dc, const RECT& rect, ViewportSession& session, const kb::scene::Scene& scene, const PresentSettings& settings);
    [[nodiscard]] bool BuildSubmission(ViewportSession& session, const kb::scene::Scene& scene, std::uint32_t width, std::uint32_t height, const PresentSettings& settings, render::Renderer::SceneFrameSubmission& submission);

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_ = nullptr;
    HWND defaultParent_ = nullptr;
    HWND deviceWindow_ = nullptr;
    HWND paintParent_ = nullptr;
    bool windowClassRegistered_ = false;
    render::Renderer renderer_;
    D2DScenePresenter scenePresenter_;
    std::vector<std::unique_ptr<ViewportSession>> sessions_;
    std::uint32_t nextDetachedViewportIndex_ = 1;
#endif
};

} // namespace kb::editor
