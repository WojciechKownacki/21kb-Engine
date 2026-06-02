#pragma once

#include "kb/render/DisplayConfig.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/SceneRenderTarget.hpp"
#include "kb/render/post/ScenePostProcessTargets.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorViewportPreviewState.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
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
    struct ViewportSession {
        HWND host = nullptr;
        std::uint32_t viewportIndex = 0;
        bool presentedInCurrentPaint = false;
        render::SceneRenderTarget sceneTarget;
        render::ScenePostProcessTargets postProcessTargets;
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
    [[nodiscard]] bool EnsureDeviceWindow(HWND parent, const RECT& rect);
    [[nodiscard]] bool EnsureRenderer(HWND parent, const RECT& rect);
    void HideSession(HWND host) noexcept;
    void ReleaseWindow(HWND window) noexcept;
    void ShutdownGpuResources() noexcept;
    void ShutdownSessionFramebuffers() noexcept;
    [[noreturn]] static void FailRender(const char* reason) noexcept;
    [[nodiscard]] bool RenderAndPresent(HDC dc, const RECT& rect, ViewportSession& session, const kb::scene::Scene& scene, const PresentSettings& settings);
    [[nodiscard]] bool BuildSubmission(ViewportSession& session, const kb::scene::Scene& scene, std::uint32_t renderWidth, std::uint32_t renderHeight, std::uint32_t outputWidth, std::uint32_t outputHeight, const PresentSettings& settings, render::Renderer::SceneFrameSubmission& submission);

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_ = nullptr;
    HWND defaultParent_ = nullptr;
    HWND deviceWindow_ = nullptr;
    HWND paintParent_ = nullptr;
    bool windowClassRegistered_ = false;
    render::Renderer renderer_;
    std::vector<std::unique_ptr<ViewportSession>> sessions_;
    std::uint32_t nextDetachedViewportIndex_ = 1;
#endif
};

} // namespace kb::editor
