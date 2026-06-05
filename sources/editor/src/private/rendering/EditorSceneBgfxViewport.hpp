#pragma once

#include "kb/render/DisplayConfig.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/SceneRenderTarget.hpp"
#include "kb/render/post/ScenePostProcessTargets.hpp"
#include "kb/render/resources/NativeWindowFramebuffer.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
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

    struct HostSurfaceLayout {
        std::uint64_t viewportKey = 0;
        RECT bounds{};
    };

    ~EditorSceneBgfxViewport();

    EditorSceneBgfxViewport(const EditorSceneBgfxViewport&) = delete;
    EditorSceneBgfxViewport& operator=(const EditorSceneBgfxViewport&) = delete;

    EditorSceneBgfxViewport() = default;

    void Configure(HINSTANCE instance, HWND parent, EditorRenderBackendSettings* backendSettings) noexcept;
    [[nodiscard]] const char* ActiveBackendLabel() const noexcept;
    void RequestPresent() noexcept;
    [[nodiscard]] bool PresentRequested() const noexcept;
    void ClearPresentRequest() noexcept;
    void SyncHostSurfaceLayouts(HWND parent, std::span<const HostSurfaceLayout> layouts) noexcept;
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
        std::uint64_t key = 0;
        HWND window = nullptr;
        RECT rect{};
        RECT layoutBounds{};
        bool presentedInCurrentPaint = false;
        bool hasLayoutBounds = false;
        render::NativeWindowFramebuffer presentTarget;
    };

    class HostSurfaceStore {
    public:
        void Clear() noexcept;
        [[nodiscard]] HostSurface* Ensure(HWND host, std::uint64_t key);
        [[nodiscard]] HostSurface* Find(HWND host, std::uint64_t key) noexcept;
        [[nodiscard]] HostSurface* FindByWindow(HWND window) noexcept;
        void MarkHostNotPresented(HWND host) noexcept;
        [[nodiscard]] bool HasVisibleUnpresentedForHost(HWND host) const noexcept;
        void Hide(HostSurface& surface) noexcept;
        void HideUnpresentedForHost(HWND host) noexcept;
        void ReleaseWindow(HWND window) noexcept;
        void ShutdownPresentTargets() noexcept;
        void DestroyWindows() noexcept;
        void ShowPresentedWindows() noexcept;

    private:
        std::vector<std::unique_ptr<HostSurface>> hostSurfaces_;
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

    class ViewportSessionStore {
    public:
        void Clear() noexcept;
        [[nodiscard]] ViewportSession* Ensure(HWND host, std::uint64_t key, std::uint32_t maxViewportIndex);
        [[nodiscard]] ViewportSession* Find(HWND host, std::uint64_t key) noexcept;
        [[nodiscard]] ViewportSession* FindByKey(std::uint64_t key) noexcept;
        void MarkHostNotPresented(HWND host) noexcept;
        void MarkAllNotPresented() noexcept;
        void ShutdownFramebuffers() noexcept;

    private:
        std::vector<std::unique_ptr<ViewportSession>> sessions_;
        std::uint32_t nextViewportIndex_ = 0;
    };

    struct PendingPresent {
        ViewportSession* session = nullptr;
        HWND host = nullptr;
        RECT surfaceRect{};
        RECT destination{};
        const kb::scene::Scene* scene = nullptr;
        PresentSettings settings{};
        std::uint32_t renderWidth = 0;
        std::uint32_t renderHeight = 0;
        std::uint32_t outputWidth = 0;
        std::uint32_t outputHeight = 0;
    };

    struct PendingPresentBatch {
        HWND host = nullptr;
        std::uint64_t viewportKey = 0;
        RECT surfaceRect{};
        std::vector<const PendingPresent*> presents;
    };

    class PendingPresentBatchBuilder {
    public:
        [[nodiscard]] static std::vector<PendingPresentBatch> Build(std::span<const PendingPresent> pendingPresents);

    private:
        [[nodiscard]] static PendingPresentBatch* FindBatch(std::vector<PendingPresentBatch>& batches, HWND host, std::uint64_t viewportKey) noexcept;
    };

    class PendingPaintSubmitter {
    public:
        explicit PendingPaintSubmitter(EditorSceneBgfxViewport& viewport) noexcept;

        [[nodiscard]] bool Submit(std::span<const PendingPresent> pendingPresents);

    private:
        [[nodiscard]] bool BuildPendingSubmissions(std::span<const PendingPresentBatch> batches);
        [[nodiscard]] bool PrepareHostSurfaceBatch(const PendingPresentBatch& batch, HostSurface*& surface);
        [[nodiscard]] bool AppendHostSubmissions(const PendingPresentBatch& batch, const HostSurface& surface);
        [[nodiscard]] bool SubmitPreparedSubmissions();

        EditorSceneBgfxViewport& viewport_;
    };

    class PendingSubmissionBuilder {
    public:
        PendingSubmissionBuilder() = delete;

        [[nodiscard]] static bool Build(
            const PendingPresent& present,
            const HostSurface& surface,
            bool clearTarget,
            render::Renderer::SceneFrameSubmission& submission);

    private:
        [[nodiscard]] static bool EnsureSessionTargets(ViewportSession& session, std::uint32_t renderWidth, std::uint32_t renderHeight);
        [[nodiscard]] static render::RenderSceneSubmitDesc BuildSubmitDesc(
            const PendingPresent& present,
            const HostSurface& surface,
            const ViewportSession& session,
            bool clearTarget);
        [[nodiscard]] static render::RenderViewportRect OutputRectFor(const PendingPresent& present, const HostSurface& surface) noexcept;
        [[nodiscard]] static std::span<const std::uint64_t> SelectedEntitySpan(const ViewportSession& session) noexcept;
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
    [[nodiscard]] HostSurface* EnsureHostSurface(HWND host, std::uint64_t key);
    [[nodiscard]] HostSurface* FindHostSurface(HWND host, std::uint64_t key) noexcept;
    [[nodiscard]] bool EnsureWindowClass();
    [[nodiscard]] bool EnsureContextWindow();
    [[nodiscard]] bool EnsureHostSurfaceWindow(HostSurface& surface, const RECT& rect, std::span<const PendingPresent* const> presents);
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

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_ = nullptr;
    HWND defaultParent_ = nullptr;
    EditorRenderBackendSettings* backendSettings_ = nullptr;
    std::uint64_t rendererBackendGeneration_ = 0;
    HWND contextWindow_ = nullptr;
    HWND paintParent_ = nullptr;
    bool windowClassRegistered_ = false;
    bool presentRequested_ = true;
    render::Renderer renderer_;
    ViewportSessionStore sessionStore_;
    HostSurfaceStore hostSurfaceStore_;
    std::vector<PendingPresent> pendingPresents_;
    std::vector<render::Renderer::SceneFrameSubmission> pendingSubmissions_;
#endif
};

} // namespace kb::editor
