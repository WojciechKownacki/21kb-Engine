#pragma once

#include "kb/render/DisplayConfig.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/frame/RenderSceneSubmitDesc.hpp"
#include "kb/render/SceneRenderTarget.hpp"
#include "kb/render/post/ScenePostProcessTargets.hpp"
#include "kb/render/resources/NativeWindowFramebuffer.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "rendering/EditorHostSurfaceLifecycle.hpp"
#include "rendering/EditorSceneViewportTextOverlay.hpp"
#include "scene/EditorViewportPreviewState.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
        std::vector<std::uint64_t> selectedEntityIds;
        std::uint64_t viewportKey = 0;
        bool editorSceneOverlaysEnabled = true;
        render::RenderSceneSubmitDesc::EditorGridDesc editorGrid{};
        render::RenderSceneSubmitDesc::EditorGizmoDesc editorGizmo{};
        std::vector<render::EditorCameraWireframeDesc> editorCameraWireframes;
        std::vector<render::EditorLightWireframeDesc> editorLightWireframes;
        std::vector<render::PhysicsDebugLine> physicsDebugLines;
        std::vector<EditorSceneViewportTextLabel> viewportTextLabels;
        render::RenderSceneSubmitDesc::EditorSelectionBoxDesc editorSelectionBox{};
        render::SceneRenderMeshPassMode meshPassMode = render::SceneRenderMeshPassMode::OpaqueAndTransparent;
        render::SceneRenderLightingConfig lightingConfig{};
        render::RenderMaterialGraphBuildContext materialGraphContext{};
        std::optional<render::ScenePostProcessSettings> postProcessSettings{};
        std::uint8_t msaaSamples = 0U;
        bool shadowPassEnabled = true;
        bool postProcessEnabled = true;
        bool selectionMaskEnabled = true;
        bool selectionOutlineEnabled = true;
        bool gpuDrivenRuntimeDispatchEnabled = true;
        bool drawSafeArea = false;
        std::uint64_t sceneRevision = 1U;
        std::uint64_t sceneDirtyBaseRevision = 1U;
        bool sceneFullSyncRequired = true;
        std::vector<std::uint64_t> dirtySceneEntityIds;
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
    void SetErrorReporter(std::function<void(std::string_view)> reporter) noexcept;
    void SetAaTraceReporter(std::function<void(std::string_view)> reporter) noexcept;
    // Point this viewport's renderer at the per-project graph shader cache so authored material
    // graphs render through their cooked GPU program instead of the CPU PBR fallback (MAT-31).
    void SetGraphShaderCacheRoot(std::string root);
    [[nodiscard]] const char* ActiveBackendLabel() const noexcept;
    void RequestPresent() noexcept;
    [[nodiscard]] bool PresentRequested() const noexcept;
    void ClearPresentRequest() noexcept;
    void SyncHostSurfaceLayouts(HWND parent, std::span<const HostSurfaceLayout> layouts) noexcept;
    void SyncHostSurfaceLayoutsForResize(HWND parent, std::span<const HostSurfaceLayout> layouts) noexcept;
    // Native child surfaces must never remain above a minimized, deactivated,
    // or DPI-reconfigured host. Resuming only schedules a normal paint, so the
    // next present recreates any size-dependent target from current bounds.
    void SetHostSurfaceSuspended(HWND host, bool suspended) noexcept;
    void SetAllHostSurfacesSuspended(bool suspended) noexcept;
    void NotifyHostDpiChanged(HWND host) noexcept;
    void ReleaseScene(const kb::scene::Scene& scene) noexcept;
    void Shutdown();
    void BeginPaintLayout() noexcept;
    void BeginPaintLayout(HWND parent) noexcept;
    void EndPaintLayout();
    void Present(HDC dc, const RECT& rect, const kb::scene::Scene& scene, const EditorTheme& theme);
    void Present(HDC dc, HWND parent, const RECT& rect, const kb::scene::Scene& scene, const EditorTheme& theme);
    void Present(HDC dc, const RECT& rect, const kb::scene::Scene& scene, const EditorTheme& theme, const PresentSettings& settings);
    void Present(HDC dc, HWND parent, const RECT& rect, const kb::scene::Scene& scene, const EditorTheme& theme, const PresentSettings& settings);
    void Present(HWND parent, const RECT& rect, const kb::scene::Scene& scene, const PresentSettings& settings);
    [[nodiscard]] bool IsHostSurfaceVisible(HWND host, std::uint64_t key) noexcept;
    // Every host surface key registered for a host window, in registration
    // order. Headless automation asserts the suspend/resume invariants over
    // all surfaces of a host instead of a single hardcoded viewport key.
    [[nodiscard]] std::vector<std::uint64_t> HostSurfaceKeysForHost(HWND host) const;
    void Hide() noexcept;

private:
    struct HostSurface {
        HWND host = nullptr;
        std::uint64_t key = 0;
        HWND clipWindow = nullptr;
        HWND window = nullptr;
        EditorSceneViewportTextOverlay textOverlay;
        RECT rect{};
        RECT layoutBounds{};
        bool presentedInCurrentPaint = false;
        bool layoutActiveInCurrentPaint = false;
        bool hasLayoutBounds = false;
        render::NativeWindowFramebuffer presentTarget;
    };

    class HostSurfaceStore {
    public:
        void Clear() noexcept;
        [[nodiscard]] HostSurface* Ensure(HWND host, std::uint64_t key);
        [[nodiscard]] HostSurface* Find(HWND host, std::uint64_t key) noexcept;
        [[nodiscard]] HostSurface* FindByWindow(HWND window) noexcept;
        [[nodiscard]] std::vector<std::uint64_t> KeysForHost(HWND host) const;
        void MarkHostNotPresented(HWND host) noexcept;
        [[nodiscard]] bool HasVisibleUnpresentedForHost(HWND host) const noexcept;
        void MarkLayoutActive(HostSurface& surface) noexcept;
        void Hide(HostSurface& surface) noexcept;
        void HideUnpresentedForHost(HWND host) noexcept;
        void HideForHost(HWND host) noexcept;
        void HideAll() noexcept;
        void ReleaseWindow(HWND window) noexcept;
        void ShutdownPresentTargets() noexcept;
        void DestroyWindows() noexcept;
        void Show(HostSurface& surface) noexcept;
        void ShowPresentedWindows() noexcept;

    private:
        std::vector<std::unique_ptr<HostSurface>> hostSurfaces_;
    };

    struct ViewportSession {
        HWND host = nullptr;
        std::uint64_t key = 0;
        std::uint32_t viewportIndex = 0;
        bool presentedInCurrentPaint = false;
        std::vector<std::uint64_t> selectedEntityIds;
        std::uint64_t submittedSceneRevision = 0U;
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
        void ResetSubmittedSceneRevisions() noexcept;

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
        [[nodiscard]] static bool EnsureSessionTargets(
            ViewportSession& session,
            std::uint32_t renderWidth,
            std::uint32_t renderHeight,
            bool postProcessEnabled,
            std::uint8_t msaaSamples);
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
    [[nodiscard]] bool EnsureHostSurfaceWindow(HostSurface& surface, const RECT& rect, bool preserveBits = false);
    [[nodiscard]] bool EnsureRenderer();
    [[nodiscard]] bool EnsurePresentTarget(HostSurface& surface, std::uint32_t width, std::uint32_t height);
    void HideHostSurface(HostSurface& surface) noexcept;
    void HideSession(HWND host, std::uint64_t key) noexcept;
    void ReleaseWindow(HWND window) noexcept;
    void ShutdownGpuResources() noexcept;
    void ShutdownSessionFramebuffers() noexcept;
    [[nodiscard]] bool SubmitPendingPaint();
    void ReportAaTrace(std::string_view message, bool force = false);
    void ReportAaRouteTrace(std::string_view message, bool force = false);
    void ReportAaPipelineTrace(std::string_view message, bool force = false);
    void SetFailureDetail(std::string detail);
    void TrackPaintHost(HWND parent) noexcept;
    void FailRender(const char* reason) noexcept;
    [[nodiscard]] bool RenderAndPresent(HDC dc, const RECT& rect, ViewportSession& session, const kb::scene::Scene& scene, const PresentSettings& settings);
    [[nodiscard]] bool QueuePresent(const RECT& rect, ViewportSession& session, const kb::scene::Scene& scene, const PresentSettings& settings);
    [[nodiscard]] static bool ShouldPreserveHostSurfaceBits(std::uint64_t viewportKey) noexcept;

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_ = nullptr;
    HWND defaultParent_ = nullptr;
    EditorRenderBackendSettings* backendSettings_ = nullptr;
    std::uint64_t rendererBackendGeneration_ = 0;
    std::uint8_t rendererMsaaSamples_ = 0;
    HWND contextWindow_ = nullptr;
    HWND paintParent_ = nullptr;
    std::vector<HWND> paintHosts_;
    bool windowClassRegistered_ = false;
    bool presentRequested_ = true;
    bool renderFailed_ = false;
    bool renderFailureReported_ = false;
    // MAT-72: wall-clock between submitted frames so animated graph materials (Time / Panner / Rotator)
    // advance u_time in the live editor, not only in tests.
    std::chrono::steady_clock::time_point lastFrameClock_{};
    bool hasLastFrameClock_ = false;
    std::function<void(std::string_view)> errorReporter_{};
    std::function<void(std::string_view)> aaTraceReporter_{};
    std::string lastConsoleAaTrace_{};
    std::string lastConsoleAaRouteTrace_{};
    std::string lastConsoleAaPipelineTrace_{};
    std::string failureDetail_{};
    std::string graphShaderCacheRoot_{};
    render::Renderer renderer_;
    ViewportSessionStore sessionStore_;
    HostSurfaceStore hostSurfaceStore_;
    EditorHostSurfaceLifecycle hostLifecycle_;
    std::vector<PendingPresent> pendingPresents_;
    std::vector<render::Renderer::SceneFrameSubmission> pendingSubmissions_;
#endif
};

} // namespace kb::editor
