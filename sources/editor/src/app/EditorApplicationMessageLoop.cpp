#include <array>
#include "app/EditorApplicationMessageLoop.hpp"
#include "app/EditorAudioAssetPreview.hpp"

#if defined(_WIN32)

#include "docking/EditorFloatingWindowManager.hpp"
#include "console/EditorConsoleState.hpp"
#include "engine/input/InputHaptics.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/platform/win32/Win32XInputHapticsBackend.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/PhysicsDebugDraw.hpp"
#include "kb/render/SceneDepthPolicy.hpp"
#include "rendering/InspectorPanelRenderer.hpp"
#include "rendering/MaterialPreviewViewportKeys.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "rendering/MaterialPreviewRenderPolicy.hpp"
#include "rendering/EditorHostSurfaceLayoutResolver.hpp"
#include "rendering/ScenePanelContentRenderer.hpp"
#include "rendering/SceneViewportPresentationPolicy.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"
#include "rendering/SkeletalMeshEditorPanelRenderer.hpp"
#include "rendering/ParticleEditorPanelRenderer.hpp"
#include "rendering/EditorMaterialThumbnailService.hpp"
#include "rendering/EditorMeshPreviewService.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "rendering/script_editor/ScriptEditorWindow.hpp"
#include "app/scene_viewport/EditorSceneViewportCameraController.hpp"
#include "app/scene_viewport/EditorViewportCameraNavigationInput.hpp"
#include "app/scene_viewport/EditorSceneViewportObjectInteraction.hpp"
#include "app/scene_viewport/EditorTerrainViewportInteraction.hpp"
#include "app/EditorWindowInvalidator.hpp"
#include "scene/EditorTerrainService.hpp"
#include "diagnostics/EditorLagTrace.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <vector>

#include <bx/math.h>

namespace kb::editor {
namespace {

// [perf] Slow-frame trace: only writes when a loop iteration exceeds a threshold, so it adds no per-frame cost
// on healthy frames. It pins down whether an "action stutters 1-2s" is the GDI message pump (panel repaints) or
// the editor tick (bgfx present + cook-result apply). Written to Saved/Logs/editor-frame-perf.log.
void WriteFramePerf(std::string_view line) {
    static std::ofstream stream = [] {
        std::error_code error;
        const std::filesystem::path path = std::filesystem::current_path() / "Saved" / "Logs" / "editor-frame-perf.log";
        std::filesystem::create_directories(path.parent_path(), error);
        return std::ofstream{ path, std::ios::out | std::ios::app };
    }();
    if (!stream.is_open()) {
        return;
    }
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    stream << millis << ' ' << line << '\n';
    stream.flush();
}

constexpr float kMaximumRuntimeDeltaSeconds = 1.0F / 15.0F;
constexpr DWORD kPausedToolbarAnimationIntervalMs = 33;
constexpr double kEditorTargetFrameRate = 60.0;
constexpr DWORD kSceneToolbarRefreshIntervalMs = 250;
constexpr int kMaxMessagesPerPump = 128;

[[nodiscard]] const char* MessageName(UINT message) noexcept {
    switch (message) {
    case WM_PAINT: return "WM_PAINT";
    case WM_LBUTTONDOWN: return "WM_LBUTTONDOWN";
    case WM_LBUTTONUP: return "WM_LBUTTONUP";
    case WM_LBUTTONDBLCLK: return "WM_LBUTTONDBLCLK";
    case WM_RBUTTONDOWN: return "WM_RBUTTONDOWN";
    case WM_RBUTTONUP: return "WM_RBUTTONUP";
    case WM_MBUTTONDOWN: return "WM_MBUTTONDOWN";
    case WM_MBUTTONUP: return "WM_MBUTTONUP";
    case WM_MOUSEMOVE: return "WM_MOUSEMOVE";
    case WM_MOUSEWHEEL: return "WM_MOUSEWHEEL";
    case WM_KEYDOWN: return "WM_KEYDOWN";
    case WM_KEYUP: return "WM_KEYUP";
    case WM_CHAR: return "WM_CHAR";
    case WM_TIMER: return "WM_TIMER";
    case WM_SIZE: return "WM_SIZE";
    case WM_WINDOWPOSCHANGED: return "WM_WINDOWPOSCHANGED";
    default: return "WM_OTHER";
    }
}

[[nodiscard]] bool IsDiagnosticInputMessage(UINT message) noexcept {
    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_KEYDOWN:
    case WM_CHAR:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] float RuntimeDeltaSeconds(std::chrono::steady_clock::time_point previous, std::chrono::steady_clock::time_point current) noexcept {
    const std::chrono::duration<float> delta = current - previous;
    return std::clamp(delta.count(), 0.0F, kMaximumRuntimeDeltaSeconds);
}

[[nodiscard]] std::chrono::steady_clock::duration EditorFrameInterval() noexcept {
    return std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / kEditorTargetFrameRate));
}

[[nodiscard]] DWORD FrameWaitMilliseconds(std::chrono::steady_clock::time_point now, std::chrono::steady_clock::time_point deadline) noexcept {
    if (now >= deadline) {
        return 0U;
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    return static_cast<DWORD>(std::clamp<long long>(remaining.count(), 1LL, 1000LL));
}

[[nodiscard]] RECT ToRect(const DockRect& rect) noexcept {
    return RECT{
        .left = rect.x,
        .top = rect.y,
        .right = rect.x + rect.width,
        .bottom = rect.y + rect.height,
    };
}

[[nodiscard]] std::uint32_t RectWidth(const RECT& rect) noexcept {
    return static_cast<std::uint32_t>(std::max<LONG>(0, rect.right - rect.left));
}

[[nodiscard]] std::uint32_t RectHeight(const RECT& rect) noexcept {
    return static_cast<std::uint32_t>(std::max<LONG>(0, rect.bottom - rect.top));
}

[[nodiscard]] float Aspect(std::uint32_t width, std::uint32_t height) noexcept {
    return height == 0U ? 1.0F : static_cast<float>(std::max(1U, width)) / static_cast<float>(height);
}

[[nodiscard]] kb::render::SceneRenderCamera BuildMaterialPreviewCamera(
    std::uint32_t renderWidth,
    std::uint32_t renderHeight,
    const EditorMaterialPreviewSceneSettings& settings) noexcept {
    kb::render::SceneRenderCamera camera{};
    const std::array<float, 3U> eye = settings.CameraEye();
    bx::mtxLookAt(camera.view.data(), bx::Vec3{eye[0], eye[1], eye[2]}, bx::Vec3{0.0F, 0.0F, 0.0F}, bx::Vec3{0.0F, 1.0F, 0.0F});
    kb::render::SceneDepthPolicy::MakePerspective(
        camera.projection.data(),
        settings.verticalFovDegrees,
        Aspect(renderWidth, renderHeight),
        0.05F,
        50.0F,
        kb::render::SceneDepthPolicy::HomogeneousDepth());
    return camera;
}

[[nodiscard]] kb::render::SceneRenderLightingConfig BuildMaterialPreviewLightingConfig(
    const EditorMaterialPreviewSceneSettings& settings,
    kb::project::ProjectSceneLightingPath projectLightingPath) noexcept {
    return MaterialPreviewRenderPolicy::NeutralPbrLightingConfig(settings, projectLightingPath);
}

[[nodiscard]] const kb::assets::AssetMetadata* MaterialMetadataForAsset(const EditorSceneContext& sceneContext, kb::assets::AssetId assetId) noexcept {
    if (!assetId.IsValid()) {
        return nullptr;
    }
    const kb::assets::AssetMetadata* metadata = sceneContext.Scene().Assets().Manager().Registry().Find(assetId);
    return metadata != nullptr && (metadata->type == "RenderMaterial" || metadata->type == "RenderMaterialInstance") ? metadata : nullptr;
}

[[nodiscard]] EditorSceneBgfxViewport::PresentSettings BuildMaterialPreviewSettings(EditorSceneContext& sceneContext, const RECT& previewRect, std::uint64_t viewportKey) {
    const std::uint32_t renderWidth = std::max<std::uint32_t>(1U, RectWidth(previewRect));
    const std::uint32_t renderHeight = std::max<std::uint32_t>(1U, RectHeight(previewRect));
    const EditorMaterialPreviewSceneSettings& previewSettings = sceneContext.MaterialPreviewSceneSettings();
    const EditorMaterialPreviewSurface previewSurface = viewportKey == kInspectorMaterialPreviewViewportKey
        ? EditorMaterialPreviewSurface::Inspector
        : EditorMaterialPreviewSurface::MaterialEditor;
    return EditorSceneBgfxViewport::PresentSettings{
        .renderWidth = renderWidth,
        .renderHeight = renderHeight,
        .fitMode = EditorViewportFitMode::Fit,
        .cameraOverride = BuildMaterialPreviewCamera(renderWidth, renderHeight, previewSettings),
        .viewportKey = viewportKey,
        .editorSceneOverlaysEnabled = false,
        .meshPassMode = kb::render::SceneRenderMeshPassMode::OpaqueAndTransparent,
        .lightingConfig = BuildMaterialPreviewLightingConfig(previewSettings, sceneContext.Project().sceneLightingPath),
        .postProcessSettings = MaterialPreviewRenderPolicy::StableExposurePostProcessSettings(previewSettings),
        .shadowPassEnabled = false,
        .postProcessEnabled = previewSettings.postProcessEnabled && !previewSettings.normalDebugView,
        .selectionMaskEnabled = false,
        .selectionOutlineEnabled = false,
        .gpuDrivenRuntimeDispatchEnabled = false,
        .drawSafeArea = false,
        .sceneRevision = sceneContext.MaterialPreviewRevision(previewSurface),
        .sceneDirtyBaseRevision = sceneContext.MaterialPreviewRevision(previewSurface),
        .sceneFullSyncRequired = true,
    };
}

[[nodiscard]] DockLayout BuildMainLayout(EditorApplicationState& state) {
    RECT client{};
    GetClientRect(state.window, &client);
    return state.dockModel.Queries().BuildLayout(
        client.right - client.left,
        client.bottom - client.top,
        state.metrics.menuHeight,
        state.metrics.toolbarHeight,
        state.metrics.tabStripHeight,
        state.metrics.tabMinWidth,
        state.metrics.tabWidth,
        state.metrics.splitterSize,
        state.metrics.panelPadding);
}

[[nodiscard]] bool ParticleEditorPanelIsVisible(const EditorApplicationState& state) {
    if (!state.sceneContext.HasParticleEditorAsset()) return false;
    RECT client{};
    GetClientRect(state.window, &client);
    const DockLayout layout = state.dockModel.Queries().BuildLayout(
        client.right - client.left,
        client.bottom - client.top,
        state.metrics.menuHeight,
        state.metrics.toolbarHeight,
        state.metrics.tabStripHeight,
        state.metrics.tabMinWidth,
        state.metrics.tabWidth,
        state.metrics.splitterSize,
        state.metrics.panelPadding);
    for (const DockPanelLayout& panelLayout : layout.panels) {
        if (!panelLayout.active) continue;
        const DockPanel* panel = state.dockModel.Queries().FindPanel(panelLayout.panelId);
        if (panel != nullptr && panel->kind == DockPanelKind::ParticleEditor) return true;
    }
    for (HWND window : state.floatingWindows.Queries().Windows()) {
        if (window == nullptr || IsWindowVisible(window) == 0) continue;
        const DockPanel* panel = state.dockModel.Queries().FindPanel(state.floatingWindows.Queries().PanelId(window));
        if (panel != nullptr && panel->kind == DockPanelKind::ParticleEditor) return true;
    }
    return false;
}

void InvalidateSceneToolbar(HWND window, const RECT& content, const EditorViewportPreviewState& preview) noexcept {
    const RECT toolbar = SceneViewportToolbarRenderer::Resolve(content, preview).toolbar;
    InvalidateRect(window, &toolbar, FALSE);
}

[[nodiscard]] bool ShouldRefreshSceneToolbars() noexcept {
    static DWORD lastRefresh = 0U;
    const DWORD now = GetTickCount();
    if (now - lastRefresh < kSceneToolbarRefreshIntervalMs) {
        return false;
    }
    lastRefresh = now;
    return true;
}

void CoalesceConsecutiveMouseMoveMessages(MSG& message) noexcept {
    if (message.message != WM_MOUSEMOVE) {
        return;
    }

    MSG next{};
    while (PeekMessageW(&next, nullptr, 0, 0, PM_NOREMOVE) != 0) {
        if (next.message != WM_MOUSEMOVE || next.hwnd != message.hwnd) {
            return;
        }
        static_cast<void>(PeekMessageW(&next, nullptr, 0, 0, PM_REMOVE));
        message = next;
    }
}

void InvalidatePanelKind(EditorApplicationState& state, DockPanelKind kind) noexcept {
    EditorWindowInvalidator::InvalidateDockPanel(
        state.window, state.dockModel, state.floatingWindows, state.metrics, kind);
}

void InvalidateInspectorPanels(EditorApplicationState& state) noexcept {
    InvalidatePanelKind(state, DockPanelKind::Inspector);
}

void InvalidateMeshPreviewPanels(EditorApplicationState& state) noexcept {
    // Mesh previews are CPU-rasterized and consumed only by Project Files and Inspector. Invalidating
    // the whole native host here also dirtied the Scene child swapchain without submitting a new scene
    // frame, allowing Windows to expose a stale back buffer as a stretched/duplicated grid.
    InvalidatePanelKind(state, DockPanelKind::Assets);
    InvalidatePanelKind(state, DockPanelKind::Inspector);
}

[[nodiscard]] bool PresentScenePanel(EditorApplicationState& state, HWND host, const DockPanel& panel, const RECT& content, bool refreshToolbar) {
    if (host == nullptr || IsWindow(host) == 0 || IsWindowVisible(host) == 0) {
        return false;
    }

    ScenePanelContentRenderer::PresentViewport(state.sceneViewport, host, content, panel, state.sceneContext, state.renderBackendSettings);
    if (refreshToolbar) {
        InvalidateSceneToolbar(host, content, state.sceneContext.ViewportPreview(panel.id));
    }
    return true;
}

[[nodiscard]] bool PresentMaterialPreview(EditorApplicationState& state, HWND host,
        const std::optional<RECT>& inspectorContent,
        const std::optional<RECT>& materialEditorContent) {
    if (host == nullptr || IsWindow(host) == 0 || IsWindowVisible(host) == 0) {
        return false;
    }

    struct PreviewTarget {
        std::optional<RECT> rect;
        std::uint64_t viewportKey;
        const kb::assets::AssetMetadata* metadata = nullptr;
    };
    const kb::assets::AssetMetadata* inspectorMetadata = MaterialMetadataForAsset(state.sceneContext, state.sceneContext.AssetBrowser().InspectorAsset());
    const kb::assets::AssetMetadata* materialEditorMetadata = MaterialMetadataForAsset(state.sceneContext, state.sceneContext.MaterialEditor().OpenAssetId());
    const PreviewTarget targets[2] = {
        PreviewTarget{
            inspectorMetadata != nullptr && inspectorContent.has_value()
                ? InspectorPanelRenderer::MaterialPreviewRect(*inspectorContent, state.sceneContext)
                : std::nullopt,
            kInspectorMaterialPreviewViewportKey,
            inspectorMetadata},
        PreviewTarget{
            materialEditorMetadata != nullptr && materialEditorContent.has_value()
                ? MaterialEditorPanelRenderer::MaterialPreviewRect(*materialEditorContent, state.sceneContext)
                : std::nullopt,
            kMaterialEditorPreviewViewportKey,
            materialEditorMetadata},
    };

    bool presented = false;
    for (const PreviewTarget& target : targets) {
        if (target.metadata != nullptr && target.rect.has_value()) {
            const EditorMaterialPreviewSurface previewSurface = target.viewportKey == kInspectorMaterialPreviewViewportKey
                ? EditorMaterialPreviewSurface::Inspector
                : EditorMaterialPreviewSurface::MaterialEditor;
            const kb::scene::Scene& previewScene = state.sceneContext.MaterialPreviewScene(target.metadata->id, previewSurface);
            const EditorSceneBgfxViewport::PresentSettings settings = BuildMaterialPreviewSettings(state.sceneContext, *target.rect, target.viewportKey);
            state.sceneViewport.Present(host, *target.rect, previewScene, settings);
            presented = true;
        }
    }
    return presented;
}

[[nodiscard]] bool QueueMainHost(EditorApplicationState& state, bool refreshToolbar, bool& scenePresented) {
    if (state.window == nullptr || IsWindowVisible(state.window) == 0) {
        return false;
    }

    const DockLayout layout = BuildMainLayout(state);
    const std::vector<EditorSceneBgfxViewport::HostSurfaceLayout> hostLayouts =
        EditorHostSurfaceLayoutResolver::ResolveMainWindow(
            state.window,
            state.dockModel,
            state.metrics,
            state.sceneContext);
    state.sceneViewport.SyncHostSurfaceLayouts(
        state.window,
        std::span<const EditorSceneBgfxViewport::HostSurfaceLayout>{hostLayouts.data(), hostLayouts.size()});
    bool documentPresented = false;
    for (const DockPanelLayout& panelLayout : layout.panels) {
        if (!panelLayout.active) {
            continue;
        }
        const DockPanel* panel = state.dockModel.Queries().FindPanel(panelLayout.panelId);
        if (panel == nullptr) {
            continue;
        }
        const RECT content = ToRect(panelLayout.content);
        if (panel->kind == DockPanelKind::Scene) {
            scenePresented = PresentScenePanel(state, state.window, *panel, content, refreshToolbar) || scenePresented;
        } else if (panel->kind == DockPanelKind::SkeletalMeshEditor) {
            documentPresented = SkeletalMeshEditorPanelRenderer::PresentViewport(
                state.sceneViewport, state.window, content, *panel,
                state.sceneContext, state.renderBackendSettings) || documentPresented;
        } else if (panel->kind == DockPanelKind::ParticleEditor) {
            documentPresented = ParticleEditorPanelRenderer::PresentViewport(
                state.sceneViewport, state.window, content, *panel,
                state.sceneContext, state.renderBackendSettings) || documentPresented;
        }
    }

    const std::optional<RECT> inspector = EditorPanelContentResolver::Resolve(
        DockPanelKind::Inspector,
        state.window,
        state.window,
        state.dockModel,
        state.floatingWindows,
        state.metrics);
    const std::optional<RECT> materialEditor = EditorPanelContentResolver::Resolve(
        DockPanelKind::MaterialEditor,
        state.window,
        state.window,
        state.dockModel,
        state.floatingWindows,
        state.metrics);
    const bool previewPresented = PresentMaterialPreview(state, state.window, inspector, materialEditor);

    // Material thumbnails: one material per frame is rendered at thumbnail resolution into a small
    // staging strip and captured from the GPU. Project Files is preferred as the staging host; the
    // Inspector keeps the queue moving when terrain layer cards are the visible consumer.
    bool thumbnailPresented = false;
    const std::optional<RECT> assets = EditorPanelContentResolver::Resolve(
        DockPanelKind::Assets,
        state.window,
        state.window,
        state.dockModel,
        state.floatingWindows,
        state.metrics);
    const std::optional<RECT>& thumbnailHost = assets.has_value() ? assets : inspector;
    if (thumbnailHost.has_value() && EditorMaterialThumbnailCache().HasPendingWork()) {
        const RECT staging{
            thumbnailHost->left,
            thumbnailHost->top,
            thumbnailHost->left + 8,
            thumbnailHost->top + 8,
        };
        const std::uint64_t revisionBefore = EditorMaterialThumbnailCache().Revision();
        EditorMaterialThumbnailCache().Tick(state.sceneContext, state.sceneViewport, state.window, staging);
        if (EditorMaterialThumbnailCache().Revision() != revisionBefore) {
            InvalidateInspectorPanels(state);
        }
        thumbnailPresented = true;
    }
    return scenePresented || documentPresented || previewPresented || thumbnailPresented;
}

[[nodiscard]] bool QueueFloatingHosts(EditorApplicationState& state, bool refreshToolbar, bool& scenePresented) {
    bool presented = false;
    const EditorFloatingWindowQueries queries = state.floatingWindows.Queries();
    for (HWND window : queries.Windows()) {
        if (window == nullptr || IsWindow(window) == 0 || IsWindowVisible(window) == 0) {
            continue;
        }

        // Register every visible native host in the current shared bgfx frame.
        // A host without an active viewport must still retire its old child surface.
        state.sceneViewport.SyncHostSurfaceLayouts(window, {});
        const DockPanel* panel = state.dockModel.Queries().FindPanel(queries.PanelId(window));
        if (panel != nullptr && panel->kind == DockPanelKind::Scene) {
            const std::optional<RECT> content = EditorPanelContentResolver::Resolve(
                DockPanelKind::Scene,
                window,
                state.window,
                state.dockModel,
                state.floatingWindows,
                state.metrics);
            if (content.has_value()) {
                scenePresented = PresentScenePanel(state, window, *panel, *content, refreshToolbar) || scenePresented;
            }
        } else if (panel != nullptr && panel->kind == DockPanelKind::SkeletalMeshEditor) {
            const std::optional<RECT> content = EditorPanelContentResolver::Resolve(
                DockPanelKind::SkeletalMeshEditor,
                window,
                state.window,
                state.dockModel,
                state.floatingWindows,
                state.metrics);
            if (content.has_value()) {
                presented = SkeletalMeshEditorPanelRenderer::PresentViewport(
                    state.sceneViewport, window, *content, *panel,
                    state.sceneContext, state.renderBackendSettings) || presented;
            }
        } else if (panel != nullptr && panel->kind == DockPanelKind::ParticleEditor) {
            const std::optional<RECT> content = EditorPanelContentResolver::Resolve(
                DockPanelKind::ParticleEditor, window, state.window, state.dockModel,
                state.floatingWindows, state.metrics);
            if (content.has_value()) {
                presented = ParticleEditorPanelRenderer::PresentViewport(
                    state.sceneViewport, window, *content, *panel,
                    state.sceneContext, state.renderBackendSettings) || presented;
            }
        }

        const std::optional<RECT> inspector = EditorPanelContentResolver::Resolve(
            DockPanelKind::Inspector,
            window,
            state.window,
            state.dockModel,
            state.floatingWindows,
            state.metrics);
        const std::optional<RECT> materialEditor = EditorPanelContentResolver::Resolve(
            DockPanelKind::MaterialEditor,
            window,
            state.window,
            state.dockModel,
            state.floatingWindows,
            state.metrics);
        const bool previewPresented = PresentMaterialPreview(state, window, inspector, materialEditor);
        presented = scenePresented || previewPresented || presented;
    }
    return presented;
}

[[nodiscard]] bool PresentVisibleViewports(EditorApplicationState& state) {
    const bool refreshToolbar = ShouldRefreshSceneToolbars();
    const bool explicitPresentRequested = state.sceneViewport.PresentRequested();
    state.sceneViewport.SetGraphShaderCacheRoot(state.sceneContext.GraphShaderCacheRoot());
    if (EditorMeshPreviewCache().PumpCompletedPreviews(state.sceneContext.Scene().Assets().Manager()) > 0U) {
        InvalidateMeshPreviewPanels(state);
    }
    const std::size_t importedItems = state.sceneContext.PumpAssetImportResults();
    if (importedItems > 0U) {
        state.sceneViewport.RequestPresent();
        InvalidateRect(state.window, nullptr, FALSE);
    }
    const auto cookStart = std::chrono::steady_clock::now();
    const std::size_t cookResults = state.sceneContext.PumpMaterialGraphCookResults();
    const double cookMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cookStart).count();
    if (cookResults > 0U) {
        state.sceneViewport.RequestPresent();
    }

    // Present only when visible GPU content can actually have changed. The previous loop ignored
    // PresentRequested() and continuously submitted every viewport at 180 Hz, keeping bgfx and the
    // graphics driver saturated even while the editor was idle. Material graphs are the one
    // intentionally time-driven preview and Play Mode advances the scene every tick.
    const bool playPresent = state.playMode.IsPlaying();
    const bool materialPresent = state.sceneContext.MaterialEditor().OpenAssetId().IsValid();
    const bool particlePresent = ParticleEditorPanelIsVisible(state);
    const bool thumbnailPresent = EditorMaterialThumbnailCache().HasPendingWork();
    const bool continuousPresent = playPresent || materialPresent || particlePresent;
    // The toolbar refresh only repaints cached counters. It must not manufacture a GPU frame:
    // doing so submitted every visible viewport exactly four times per second while idle.
    if (continuousPresent || thumbnailPresent) {
        state.sceneViewport.RequestPresent();
    }
    if (!state.sceneViewport.PresentRequested()) {
        return false;
    }

    bool scenePresented = false;
    const auto beginStart = std::chrono::steady_clock::now();
    state.sceneViewport.BeginPaintLayout(state.window);
    const double beginMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - beginStart).count();
    const auto mainStart = std::chrono::steady_clock::now();
    const bool mainPresented = QueueMainHost(state, refreshToolbar, scenePresented);
    const double mainMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mainStart).count();
    const auto floatStart = std::chrono::steady_clock::now();
    const bool floatingPresented = QueueFloatingHosts(state, refreshToolbar, scenePresented);
    const double floatMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - floatStart).count();
    const auto endStart = std::chrono::steady_clock::now();
    state.sceneViewport.EndPaintLayout();
    const double endMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - endStart).count();
    const double presentMs = beginMs + mainMs + floatMs + endMs;
    if (cookMs > 25.0 || presentMs > 25.0) {
        std::ostringstream row;
        row << "[present] cook=" << cookMs << "ms(r=" << cookResults << ") begin=" << beginMs
            << "ms mainHost=" << mainMs << "ms floatHost=" << floatMs << "ms endFrame=" << endMs
            << "ms scene=" << (scenePresented ? 1 : 0)
            << " reason(explicit=" << (explicitPresentRequested ? 1 : 0)
            << ",toolbar=" << (refreshToolbar ? 1 : 0)
            << ",play=" << (playPresent ? 1 : 0)
            << ",material=" << (materialPresent ? 1 : 0)
            << ",thumbnail=" << (thumbnailPresent ? 1 : 0)
            << ",import=" << (importedItems > 0U ? 1 : 0)
            << ",cook=" << (cookResults > 0U ? 1 : 0) << ')';
        WriteFramePerf(row.str());
        diagnostics::EditorLagTrace::Slow(
            "present-cycle",
            diagnostics::EditorLagTrace::NextEventId(),
            std::max(cookMs, presentMs),
            row.str(),
            25.0);
    }
    if (mainPresented || floatingPresented) {
        state.sceneViewport.ClearPresentRequest();
    }
    if (scenePresented) {
        state.sceneContext.AcknowledgeSceneRenderSubmitted();
    }
    return mainPresented || floatingPresented;
}

void ConfigurePlayModePointerViewport(EditorApplicationState& state) {
    HWND coordinateWindow = state.window;
    std::optional<EditorResolvedPanelContent> scenePanel;

    POINT cursor{};
    if (GetCursorPos(&cursor) != 0) {
        const HWND hitWindow = WindowFromPoint(cursor);
        const HWND rootWindow = hitWindow != nullptr
            ? GetAncestor(hitWindow, GA_ROOT)
            : nullptr;
        if (rootWindow != nullptr && rootWindow != state.window) {
            scenePanel = EditorPanelContentResolver::ResolvePanel(
                DockPanelKind::Scene,
                rootWindow,
                state.window,
                state.dockModel,
                state.floatingWindows,
                state.metrics);
            if (scenePanel.has_value()) {
                coordinateWindow = rootWindow;
            }
        }
    }
    if (!scenePanel.has_value()) {
        coordinateWindow = state.window;
        scenePanel = EditorPanelContentResolver::ResolvePanel(
            DockPanelKind::Scene,
            state.window,
            state.window,
            state.dockModel,
            state.floatingWindows,
            state.metrics);
    }
    if (!scenePanel.has_value()) {
        state.inputCollector.ClearPointerViewport();
        return;
    }

    const EditorViewportPreviewState& preview =
        state.sceneContext.ViewportPreview(scenePanel->panelId);
    const RECT renderArea =
        SceneViewportToolbarRenderer::Resolve(scenePanel->content, preview, state.sceneContext).renderArea;
    const std::uint32_t displayWidth = static_cast<std::uint32_t>(
        std::max<LONG>(0, renderArea.right - renderArea.left));
    const std::uint32_t displayHeight = static_cast<std::uint32_t>(
        std::max<LONG>(0, renderArea.bottom - renderArea.top));
    if (displayWidth == 0U || displayHeight == 0U) {
        state.inputCollector.ClearPointerViewport();
        return;
    }
    state.inputCollector.ConfigurePointerViewport(
        coordinateWindow,
        renderArea,
        preview.RenderWidthForPanel(displayWidth),
        preview.RenderHeightForPanel(displayHeight));
}

void TickPlayMode(EditorApplicationState& state, float deltaSeconds) {
    // LIB-153: the editor is the host that owns the physical XInput devices, so it owns
    // the haptics actuator too. Function-local static: one process-wide backend whose
    // destructor (and the not-playing branch below) silences every motor - leaving Play
    // Mode must never leave a pad buzzing.
    static kb::input::Win32XInputHapticsBackend hapticsBackend;
    static bool hapticsActive = false;
    if (!state.playMode.IsPlaying()) {
        if (hapticsActive) {
            hapticsBackend.StopAll();
            hapticsActive = false;
        }
        return;
    }
    if (!kb::input::InputHaptics::HasBackend(state.sceneContext.Scene())) {
        kb::input::InputHaptics::RegisterBackend(state.sceneContext.Scene(), hapticsBackend);
    }
    hapticsActive = true;
    // Feed real device input to the runtime before systems tick (Input phase).
    kb::input::InputSubsystem& input = state.sceneContext.Scene().Input();
    ConfigurePlayModePointerViewport(state);
    state.inputCollector.Collect(input.MutableDeviceState(), state.window);
    if (!state.sceneContext.TickPlayModeSceneSession(deltaSeconds)) {
        const bool previousPlayModeSceneActive = state.sceneContext.HasPlayModeSceneSession();
        state.playMode.Stop();
        static_cast<void>(state.sceneContext.RestorePlayModeSceneSession());
        if (SceneViewportPresentationPolicy::RequiresPresent(
                previousPlayModeSceneActive,
                state.sceneContext.HasPlayModeSceneSession())) {
            state.sceneViewport.RequestPresent();
            if (state.window != nullptr) {
                InvalidateRect(state.window, nullptr, FALSE);
            }
        }
    }
}

[[nodiscard]] bool TickEditorFrame(EditorApplicationState& state, float deltaSeconds) {
    const bool asyncAssetsPending = state.sceneContext.HasPendingSkeletalMeshEditorOpen() ||
        EditorMeshPreviewCache().HasPendingPreviewWork();
    if (asyncAssetsPending) {
        // AssetManager publishes worker results only on its owner thread. Keep
        // this as the single editor-loop pump for both document and thumbnail
        // requests so input dispatch never waits for asset parsing.
        state.sceneContext.Scene().Assets().Manager().PumpAsyncLoads();
    }
    const bool skeletalMeshEditorOpenChanged = state.sceneContext.PumpPendingSkeletalMeshEditorOpen();
    if (skeletalMeshEditorOpenChanged) {
        state.sceneViewport.RequestPresent();
        if (state.window != nullptr) {
            InvalidateRect(state.window, nullptr, FALSE);
        }
        for (HWND window : state.floatingWindows.Queries().Windows()) {
            if (window != nullptr && IsWindow(window) != 0) {
                InvalidateRect(window, nullptr, FALSE);
            }
        }
    }

    // Keep the scene's physics debug-draw flag in sync with the editor's collider
    // gizmo toggle (the flag lives in per-scene state and resets on reload, so a
    // cheap per-frame write keeps green collider wireframes on across reloads).
    kb::scene::PhysicsDebugDraw::SetEnabled(state.sceneContext.Scene(), state.sceneContext.ArePhysicsGizmosVisible());

    const bool terrainStrokeChanged =
        EditorTerrainViewportInteraction::TickActiveStroke(state.sceneContext, deltaSeconds);
    if (terrainStrokeChanged) {
        state.sceneViewport.RequestPresent();
    }

    bool navigationChanged = false;
    if (state.sceneContext.HasActiveViewportCameraNavigation()) {
        navigationChanged = EditorSceneViewportCameraController{
            state.window,
            state.dockModel,
            state.floatingWindows,
            state.metrics,
            state.sceneContext,
            state.sceneViewport,
        }.TickActiveNavigation(deltaSeconds);
    }

    const bool gizmoChanged = EditorSceneViewportObjectInteraction::TickGizmoDrag(
        state.window,
        state.dockModel,
        state.floatingWindows,
        state.metrics,
        state.sceneContext);
    if (gizmoChanged) {
        state.sceneViewport.RequestPresent();
        InvalidateInspectorPanels(state);
    }

    // Advance the "frame selected" (F) camera animation. Returning true keeps
    // the loop pacing at frame rate (instead of parking in WaitMessage) until
    // the ease completes, and requests a present so each interpolated step is
    // rendered.
    const bool focusChanged = state.sceneContext.TickViewportFocusAnimations(deltaSeconds);
    if (focusChanged) {
        state.sceneViewport.RequestPresent();
    }

    const bool animationPreviewCameraChanged = state.sceneContext.TickAnimationPreviewCamera(
        deltaSeconds, ReadEditorViewportCameraFlightInput());
    const bool animationPreviewPlaybackChanged = state.sceneContext.TickAnimationPreviewPlayback(deltaSeconds);
    if (animationPreviewCameraChanged || animationPreviewPlaybackChanged) {
        state.sceneViewport.RequestPresent();
    }
    const bool particlePanelVisible = ParticleEditorPanelIsVisible(state);
    const bool particlePreviewChanged =
        particlePanelVisible && state.sceneContext.TickParticleEditorPreview(deltaSeconds);
    if (particlePreviewChanged) state.sceneViewport.RequestPresent();
    const bool sceneParticlesChanged =
        !state.playMode.IsPlaying() && state.sceneContext.TickEditorSceneParticles(deltaSeconds);
    if (sceneParticlesChanged) state.sceneViewport.RequestPresent();

    const bool audioPreviewChanged = EditorAudioAssetPreview::Tick(state.sceneContext.Scene());
    if (audioPreviewChanged) {
        EditorWindowInvalidator::InvalidateDockPanel(
            state.window, state.dockModel, state.floatingWindows, state.metrics, DockPanelKind::Assets);
    }

    // Saving a script in the Script Editor (Ctrl+S) writes the file but leaves the
    // cached asset stale; detect the save here and reload it so the Inspector's
    // exposed-variable schema reflects the edit immediately (no editor/scene save
    // required). SaveSerial is monotonic, so a single equality check suffices.
    bool scriptSaved = false;
    if (const std::uint64_t saveSerial = ScriptEditorWindow::SaveSerial(); saveSerial != state.lastScriptSaveSerial) {
        state.lastScriptSaveSerial = saveSerial;
        if (state.sceneContext.ReloadOpenScriptAsset()) {
            InvalidateInspectorPanels(state);
            scriptSaved = true;
        }
    }

    // Advance Inspector slide animations. Keep the loop pacing at frame rate
    // instead of parking in WaitMessage, and repaint every interpolated step.
    const bool addComponentSliding = state.sceneContext.Inspector().TickAddComponentSlide(deltaSeconds);
    const bool disclosureSliding = state.sceneContext.Inspector().TickDisclosures(deltaSeconds);
    if (addComponentSliding || disclosureSliding) {
        InvalidateInspectorPanels(state);
    }

    const bool viewportsPresented = PresentVisibleViewports(state);
    return viewportsPresented || navigationChanged || gizmoChanged || focusChanged ||
        animationPreviewCameraChanged || animationPreviewPlaybackChanged || particlePreviewChanged ||
        sceneParticlesChanged || scriptSaved ||
        audioPreviewChanged || addComponentSliding || disclosureSliding || skeletalMeshEditorOpenChanged ||
        EditorTerrainService::ToolState().strokeActive;
}

[[nodiscard]] bool TickPointerDragFrame(EditorApplicationState& state) {
    EditorPointerDragState& drag = state.pointerDrag;
    if (!drag.Active()) {
        return false;
    }

    bool changed = false;
    HWND sourceWindow = reinterpret_cast<HWND>(drag.dragSourceWindow);
    if (sourceWindow == nullptr || IsWindow(sourceWindow) == 0) {
        sourceWindow = state.window;
    }

    if (drag.assetCreatesMeshEntity && drag.meshPreviewUpdatePending) {
        drag.meshPreviewUpdatePending = false;
        if (EditorSceneViewportObjectInteraction::UpdateMeshDragPreview(
                sourceWindow,
                state.window,
                drag.x,
                drag.y,
                state.dockModel,
                state.floatingWindows,
                state.metrics,
                state.sceneContext,
                drag)) {
            state.sceneViewport.RequestPresent();
            changed = true;
        }
    }

    if (drag.overlayDirty) {
        drag.overlayDirty = false;
        InvalidateRect(state.window, nullptr, FALSE);
        if (sourceWindow != state.window) {
            InvalidateRect(sourceWindow, nullptr, FALSE);
        }
        changed = true;
    }

    return changed;
}

} // namespace

void EditorApplicationMessageLoop::Run(EditorApplicationState& state) {
    diagnostics::EditorLagTrace::Marker("session", "message-loop-start");
    MSG message{};
    auto previousTick = std::chrono::steady_clock::now();
    auto nextEditorFrame = previousTick;
    const auto editorFrameInterval = EditorFrameInterval();
    while (state.running) {
        const auto pumpStart = std::chrono::steady_clock::now();
        int pumpedMessages = 0;
        bool dispatchedPaint = false;
        double paintMs = 0.0;
        while (pumpedMessages < kMaxMessagesPerPump && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
            ++pumpedMessages;
            if (message.message == WM_QUIT) {
                state.running = false;
                break;
            }
            const bool isPaint = message.message == WM_PAINT;
            dispatchedPaint = dispatchedPaint || isPaint;
            CoalesceConsecutiveMouseMoveMessages(message);
            const std::uint64_t messageEventId = diagnostics::EditorLagTrace::NextEventId();
            if (IsDiagnosticInputMessage(message.message)) {
                const auto x = static_cast<std::int64_t>(static_cast<short>(LOWORD(message.lParam)));
                const auto y = static_cast<std::int64_t>(static_cast<short>(HIWORD(message.lParam)));
                diagnostics::EditorLagTrace::Input(
                    messageEventId,
                    MessageName(message.message),
                    reinterpret_cast<std::uintptr_t>(message.hwnd),
                    x,
                    y);
            }
            TranslateMessage(&message);
            const auto dispatchStart = std::chrono::steady_clock::now();
            DispatchMessageW(&message);
            const double dispatchMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - dispatchStart).count();
            if (dispatchMs >= 8.0) {
                std::ostringstream detail;
                detail << "message=" << MessageName(message.message)
                       << " id=0x" << std::hex << message.message
                       << " hwnd=0x" << reinterpret_cast<std::uintptr_t>(message.hwnd) << std::dec;
                diagnostics::EditorLagTrace::Slow("dispatch", messageEventId, dispatchMs, detail.str());
            }
            if (isPaint) {
                paintMs += dispatchMs;
            }
        }
        const double pumpMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - pumpStart).count();
        if (!state.running) {
            break;
        }

        const auto currentTick = std::chrono::steady_clock::now();
        if (!state.playMode.IsPaused() && currentTick < nextEditorFrame) {
            if (pumpMs > 40.0) {
                std::ostringstream row;
                row << "[frame] pump=" << pumpMs << "ms wmpaint=" << paintMs << "ms(" << (dispatchedPaint ? 1 : 0)
                    << ") msgs=" << pumpedMessages << " tick=0ms (paced-wait)";
                WriteFramePerf(row.str());
                diagnostics::EditorLagTrace::Slow(
                    "message-pump",
                    diagnostics::EditorLagTrace::NextEventId(),
                    pumpMs,
                    row.str(),
                    40.0);
            }
            const DWORD waitMs = FrameWaitMilliseconds(currentTick, nextEditorFrame);
            static_cast<void>(MsgWaitForMultipleObjects(0, nullptr, FALSE, waitMs, QS_ALLINPUT));
            continue;
        }

        const float deltaSeconds = RuntimeDeltaSeconds(previousTick, currentTick);
        previousTick = currentTick;
        const auto tickStart = std::chrono::steady_clock::now();
        TickPlayMode(state, deltaSeconds);
        static_cast<void>(TickPointerDragFrame(state));
        const bool sceneFramePresented = TickEditorFrame(state, deltaSeconds);
        const double tickMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tickStart).count();
        if (pumpMs > 40.0 || tickMs > 40.0) {
            std::ostringstream row;
            row << "[frame] pump=" << pumpMs << "ms wmpaint=" << paintMs << "ms(" << (dispatchedPaint ? 1 : 0)
                << ") msgs=" << pumpedMessages << " tick=" << tickMs << "ms";
            WriteFramePerf(row.str());
            diagnostics::EditorLagTrace::Slow(
                "editor-loop",
                diagnostics::EditorLagTrace::NextEventId(),
                std::max(pumpMs, tickMs),
                row.str(),
                40.0);
        }
        nextEditorFrame = currentTick + editorFrameInterval;

        if (state.playMode.IsPaused()) {
            if (state.window != nullptr) {
                InvalidateRect(state.window, nullptr, FALSE);
            }
            static_cast<void>(MsgWaitForMultipleObjects(0, nullptr, FALSE, kPausedToolbarAnimationIntervalMs, QS_ALLINPUT));
        } else if (state.playMode.IsPlaying()) {
            // Play advances a frame every loop iteration, but TickEditorFrame only
            // presents the scene viewport (its own swapchain) — the GDI panels
            // (Console, Hierarchy, Inspector) only redraw on WM_PAINT. Without
            // invalidating here, live output produced during play (a script's
            // Console log, the HUD, runtime stats) lands in panel state but is not
            // drawn until the next input event forces a repaint. Invalidate each
            // play frame — as the paused branch already does — so it shows live.
            if (state.window != nullptr) {
                InvalidateRect(state.window, nullptr, FALSE);
            }
        } else if (!sceneFramePresented) {
            // Keep the loop paced (instead of parking in WaitMessage) while a material is open so
            // async graph cook results keep pumping; time-driven preview animation (MAT-72) is
            // carried by the per-frame preview presents in TickEditorFrame.
            if (state.sceneContext.MaterialEditor().OpenAssetId().IsValid() ||
                EditorAudioAssetPreview::HasActivePreview() ||
                state.sceneContext.AssetImportInProgress() ||
                state.sceneContext.HasPendingSkeletalMeshEditorOpen() ||
                EditorMeshPreviewCache().HasPendingPreviewWork()) {
                static_cast<void>(MsgWaitForMultipleObjects(0, nullptr, FALSE, FrameWaitMilliseconds(currentTick, nextEditorFrame), QS_ALLINPUT));
            } else {
                WaitMessage();
            }
        }
    }
}

} // namespace kb::editor

#endif
