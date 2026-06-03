#pragma once

#include "kb/render/DisplayConfig.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/SceneRenderTarget.hpp"
#include "kb/render/post/ScenePostProcessTargets.hpp"
#include "kb/render/resources/NativeWindowFramebuffer.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorViewportPreviewState.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
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
        std::array<std::uint64_t, 1U> selectedEntityIds{};
        std::uint64_t viewportKey = 0;
        bool editorSceneOverlaysEnabled = true;
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
    struct HostSurface {
        HWND host = nullptr;
        HWND window = nullptr;
        RECT rect{};
        bool presentedInCurrentPaint = false;
        render::NativeWindowFramebuffer presentTarget;
    };

    struct ViewportSession {
        HWND host = nullptr;
        std::uint64_t key = 0;
        std::uint32_t viewportIndex = 0;
        bool presentedInCurrentPaint = false;
        std::array<std::uint64_t, 1U> selectedEntityIds{};
        render::SceneRenderTarget sceneTarget;
        render::ScenePostProcessTargets postProcessTargets;
    };

    struct PendingPresent {
        ViewportSession* session = nullptr;
        HWND host = nullptr;
        RECT destination{};
        const kb::scene::Scene* scene = nullptr;
        PresentSettings settings{};
        std::uint32_t renderWidth = 0;
        std::uint32_t renderHeight = 0;
        std::uint32_t outputWidth = 0;
        std::uint32_t outputHeight = 0;
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

    [[nodiscard]] ViewportSession* EnsureSession(HWND host, std::uint64_t key);
    [[nodiscard]] ViewportSession* FindSession(HWND host, std::uint64_t key) noexcept;
    [[nodiscard]] ViewportSession* FindSessionByKey(std::uint64_t key) noexcept;
    [[nodiscard]] HostSurface* EnsureHostSurface(HWND host);
    [[nodiscard]] HostSurface* FindHostSurface(HWND host) noexcept;
    [[nodiscard]] HostSurface* FindHostSurfaceByWindow(HWND window) noexcept;
    [[nodiscard]] bool EnsureWindowClass();
    [[nodiscard]] bool EnsureContextWindow();
    [[nodiscard]] bool EnsureHostSurfaceWindow(HostSurface& surface, const RECT& rect, std::span<const PendingPresent*> presents);
    [[nodiscard]] bool EnsureRenderer();
    [[nodiscard]] bool EnsurePresentTarget(HostSurface& surface, std::uint32_t width, std::uint32_t height);
    void HideHostSurface(HostSurface& surface) noexcept;
    void HideSession(HWND host, std::uint64_t key) noexcept;
    void ReleaseWindow(HWND window) noexcept;
    void ShutdownGpuResources() noexcept;
    void ShutdownSessionFramebuffers() noexcept;
    [[nodiscard]] bool SubmitPendingPaint();
    [[noreturn]] static void FailRender(const char* reason) noexcept;
    [[nodiscard]] bool RenderAndPresent(HDC dc, const RECT& rect, ViewportSession& session, const kb::scene::Scene& scene, const PresentSettings& settings);
    [[nodiscard]] bool QueuePresent(const RECT& rect, ViewportSession& session, const kb::scene::Scene& scene, const PresentSettings& settings);
    [[nodiscard]] bool BuildSubmission(const PendingPresent& present, const HostSurface& surface, render::Renderer::SceneFrameSubmission& submission);

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_ = nullptr;
    HWND defaultParent_ = nullptr;
    HWND contextWindow_ = nullptr;
    HWND paintParent_ = nullptr;
    bool windowClassRegistered_ = false;
    render::Renderer renderer_;
    std::vector<std::unique_ptr<ViewportSession>> sessions_;
    std::vector<std::unique_ptr<HostSurface>> hostSurfaces_;
    std::vector<PendingPresent> pendingPresents_;
    std::vector<render::Renderer::SceneFrameSubmission> pendingSubmissions_;
    std::uint32_t nextViewportIndex_ = 0;
#endif
};

} // namespace kb::editor
