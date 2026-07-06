#include "app/EditorApplicationMessageLoop.hpp"

#if defined(_WIN32)

#include "app/EditorCrashBreadcrumbs.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "console/EditorConsoleState.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "kb/render/SceneDepthPolicy.hpp"
#include "rendering/InspectorPanelRenderer.hpp"
#include "rendering/MaterialPreviewViewportKeys.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "rendering/MaterialPreviewRenderPolicy.hpp"
#include "rendering/EditorHostSurfaceLayoutResolver.hpp"
#include "rendering/ScenePanelContentRenderer.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "app/scene_viewport/EditorSceneViewportCameraController.hpp"
#include "app/scene_viewport/EditorSceneViewportObjectInteraction.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <span>
#include <sstream>
#include <vector>

#include <bx/math.h>

namespace kb::editor {
namespace {

constexpr float kMaximumRuntimeDeltaSeconds = 1.0F / 15.0F;
constexpr DWORD kPausedToolbarAnimationIntervalMs = 33;
constexpr double kEditorTargetFrameRate = 180.0;
constexpr DWORD kSceneToolbarRefreshIntervalMs = 250;
constexpr int kMaxMessagesPerPump = 128;

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
    bx::mtxLookAt(camera.view.data(), bx::Vec3{0.0F, 0.0F, -settings.cameraDistance}, bx::Vec3{0.0F, 0.0F, 0.0F}, bx::Vec3{0.0F, 1.0F, 0.0F});
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
        .postProcessEnabled = previewSettings.postProcessEnabled,
        .selectionMaskEnabled = false,
        .selectionOutlineEnabled = false,
        .gpuDrivenRuntimeDispatchEnabled = false,
        .drawSafeArea = false,
        .sceneRevision = sceneContext.MaterialPreviewRevision(),
        .sceneDirtyBaseRevision = sceneContext.MaterialPreviewRevision(),
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

void InvalidateInspectorPanels(EditorApplicationState& state) noexcept {
    if (state.window != nullptr && IsWindow(state.window) != 0) {
        if (const std::optional<RECT> inspector = EditorPanelContentResolver::Resolve(
                DockPanelKind::Inspector,
                state.window,
                state.window,
                state.dockModel,
                state.floatingWindows,
                state.metrics)) {
            InvalidateRect(state.window, &*inspector, FALSE);
        }
    }

    const EditorFloatingWindowQueries queries = state.floatingWindows.Queries();
    for (HWND window : queries.Windows()) {
        if (window == nullptr || IsWindow(window) == 0) {
            continue;
        }
        if (const std::optional<RECT> inspector = EditorPanelContentResolver::Resolve(
                DockPanelKind::Inspector,
                window,
                state.window,
                state.dockModel,
                state.floatingWindows,
                state.metrics)) {
            InvalidateRect(window, &*inspector, FALSE);
        }
    }
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
            const kb::scene::Scene& previewScene = state.sceneContext.MaterialPreviewScene(target.metadata->id);
            const EditorSceneBgfxViewport::PresentSettings settings = BuildMaterialPreviewSettings(state.sceneContext, *target.rect, target.viewportKey);
            state.sceneViewport.Present(host, *target.rect, previewScene, settings);
            presented = true;
        }
    }
    return presented;
}

[[nodiscard]] bool PresentMainHost(EditorApplicationState& state, bool refreshToolbar) {
    if (state.window == nullptr || IsWindowVisible(state.window) == 0) {
        return false;
    }

    // Keep the scene viewport renderer pointed at the project graph shader cache and consume any
    // graph cooks that finished since the last paint so authored programs swap in live (MAT-31/33).
    state.sceneViewport.SetGraphShaderCacheRoot(state.sceneContext.GraphShaderCacheRoot());
    static_cast<void>(state.sceneContext.PumpMaterialGraphCookResults());

    bool scenePresented = false;
    const DockLayout layout = BuildMainLayout(state);
    const std::vector<EditorSceneBgfxViewport::HostSurfaceLayout> hostLayouts =
        EditorHostSurfaceLayoutResolver::ResolveMainWindow(
            state.window,
            state.dockModel,
            state.metrics,
            state.sceneContext);
    state.sceneViewport.BeginPaintLayout(state.window);
    state.sceneViewport.SyncHostSurfaceLayouts(
        state.window,
        std::span<const EditorSceneBgfxViewport::HostSurfaceLayout>{hostLayouts.data(), hostLayouts.size()});
    for (const DockPanelLayout& panelLayout : layout.panels) {
        if (!panelLayout.active) {
            continue;
        }
        const DockPanel* panel = state.dockModel.Queries().FindPanel(panelLayout.panelId);
        if (panel == nullptr || panel->kind != DockPanelKind::Scene) {
            continue;
        }
        scenePresented = PresentScenePanel(state, state.window, *panel, ToRect(panelLayout.content), refreshToolbar) || scenePresented;
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
    state.sceneViewport.EndPaintLayout();
    if (scenePresented || previewPresented) {
        state.sceneViewport.ClearPresentRequest();
    }
    if (scenePresented) {
        state.sceneContext.AcknowledgeSceneRenderSubmitted();
    }
    return scenePresented || previewPresented;
}

[[nodiscard]] bool PresentFloatingHosts(EditorApplicationState& state, bool refreshToolbar) {
    bool presented = false;
    const EditorFloatingWindowQueries queries = state.floatingWindows.Queries();
    for (HWND window : queries.Windows()) {
        if (window == nullptr || IsWindow(window) == 0 || IsWindowVisible(window) == 0) {
            continue;
        }

        bool scenePresented = false;
        state.sceneViewport.BeginPaintLayout(window);
        const DockPanel* panel = state.dockModel.Queries().FindPanel(queries.PanelId(window));
        if (panel != nullptr && panel->kind == DockPanelKind::Scene) {
            RECT content{};
            GetClientRect(window, &content);
            content.top += state.metrics.floatingChromeHeight;
            scenePresented = PresentScenePanel(state, window, *panel, content, refreshToolbar);
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
        state.sceneViewport.EndPaintLayout();
        if (scenePresented || previewPresented) {
            state.sceneViewport.ClearPresentRequest();
        }
        if (scenePresented) {
            state.sceneContext.AcknowledgeSceneRenderSubmitted();
        }
        presented = scenePresented || previewPresented || presented;
    }
    return presented;
}

[[nodiscard]] bool PresentVisibleViewports(EditorApplicationState& state) {
    const bool refreshToolbar = ShouldRefreshSceneToolbars();
    const bool mainPresented = PresentMainHost(state, refreshToolbar);
    const bool floatingPresented = PresentFloatingHosts(state, refreshToolbar);
    return mainPresented || floatingPresented;
}

void TickPlayMode(EditorApplicationState& state, float deltaSeconds) {
    if (!state.playMode.IsPlaying()) {
        return;
    }
    // Feed real device input to the runtime before systems tick (Input phase).
    kb::input::InputSubsystem& input = state.sceneContext.Scene().Input();
    state.inputCollector.Collect(input.MutableDeviceState(), state.window);
    kb::scene::SceneRuntime runtime = state.sceneContext.Scene().Runtime();
    if (!runtime.EcsProfilerEnabled()) {
        runtime.SetEcsProfilerEnabled(true);
    }
    static_cast<void>(runtime.Update(deltaSeconds));
    state.sceneContext.MarkSceneRenderDirty();
    if (state.sceneContext.Scene().Runtime().ShouldQuit()) {
        state.playMode.Stop();
        static_cast<void>(state.sceneContext.RestorePlayModeSceneSession());
    }
}

[[nodiscard]] bool TickEditorFrame(EditorApplicationState& state, float deltaSeconds) {
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

    const bool viewportsPresented = PresentVisibleViewports(state);
    return viewportsPresented || navigationChanged || gizmoChanged;
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
    MSG message{};
    auto previousTick = std::chrono::steady_clock::now();
    auto nextEditorFrame = previousTick;
    const auto editorFrameInterval = EditorFrameInterval();
    auto kbPerfLastPumpEnd = std::chrono::steady_clock::now();
    while (state.running) {
        const auto kbPerfPumpStart = std::chrono::steady_clock::now();
        int pumpedMessages = 0;
        int kbPerfMouseMoveCount = 0;
        int kbPerfPaintCount = 0;
        while (pumpedMessages < kMaxMessagesPerPump && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
            ++pumpedMessages;
            if (message.message == WM_MOUSEMOVE) {
                ++kbPerfMouseMoveCount;
            } else if (message.message == WM_PAINT) {
                ++kbPerfPaintCount;
            }
            if (message.message == WM_QUIT) {
                state.running = false;
                break;
            }
            CoalesceConsecutiveMouseMoveMessages(message);
            TranslateMessage(&message);
            const auto kbPerfDispatchStart = std::chrono::steady_clock::now();
            DispatchMessageW(&message);
            const auto kbPerfDispatchMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - kbPerfDispatchStart).count();
            if (kbPerfDispatchMicros > 1000) {
                std::ostringstream kbPerfSlowDispatchLine;
                kbPerfSlowDispatchLine << "Slow DispatchMessageW message=0x" << std::hex << message.message << std::dec
                                       << " wparam=" << message.wParam
                                       << " lparam=" << message.lParam
                                       << " took=" << kbPerfDispatchMicros << "us";
                EditorCrashBreadcrumbs::Write("perf_slowmsg", kbPerfSlowDispatchLine.str());
            }
        }
        const auto kbPerfPumpEnd = std::chrono::steady_clock::now();
        if (pumpedMessages > 0) {
            std::ostringstream kbPerfLoopLine;
            kbPerfLoopLine << "MessageLoop pump pumped=" << pumpedMessages
                           << " mouseMoves=" << kbPerfMouseMoveCount
                           << " paints=" << kbPerfPaintCount
                           << " hitCap=" << (pumpedMessages >= kMaxMessagesPerPump ? 1 : 0)
                           << " pumpDuration=" << std::chrono::duration_cast<std::chrono::microseconds>(kbPerfPumpEnd - kbPerfPumpStart).count() << "us"
                           << " sinceLastPumpEnd=" << std::chrono::duration_cast<std::chrono::microseconds>(kbPerfPumpStart - kbPerfLastPumpEnd).count() << "us";
            EditorCrashBreadcrumbs::Write("perf_loop", kbPerfLoopLine.str());
        }
        kbPerfLastPumpEnd = kbPerfPumpEnd;
        if (!state.running) {
            break;
        }

        const auto currentTick = std::chrono::steady_clock::now();
        if (!state.playMode.IsPaused() && currentTick < nextEditorFrame) {
            const DWORD waitMs = FrameWaitMilliseconds(currentTick, nextEditorFrame);
            static_cast<void>(MsgWaitForMultipleObjects(0, nullptr, FALSE, waitMs, QS_ALLINPUT));
            continue;
        }

        const float deltaSeconds = RuntimeDeltaSeconds(previousTick, currentTick);
        previousTick = currentTick;
        TickPlayMode(state, deltaSeconds);
        static_cast<void>(TickPointerDragFrame(state));
        const auto kbPerfTickFrameStart = std::chrono::steady_clock::now();
        const bool sceneFramePresented = TickEditorFrame(state, deltaSeconds);
        const auto kbPerfTickFrameMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - kbPerfTickFrameStart).count();
        if (kbPerfTickFrameMicros > 500) {
            std::ostringstream kbPerfTickLine;
            kbPerfTickLine << "TickEditorFrame took=" << kbPerfTickFrameMicros << "us presented=" << (sceneFramePresented ? 1 : 0);
            EditorCrashBreadcrumbs::Write("perf_tickframe", kbPerfTickLine.str());
        }
        nextEditorFrame = currentTick + editorFrameInterval;

        if (state.playMode.IsPaused()) {
            if (state.window != nullptr) {
                InvalidateRect(state.window, nullptr, FALSE);
            }
            static_cast<void>(MsgWaitForMultipleObjects(0, nullptr, FALSE, kPausedToolbarAnimationIntervalMs, QS_ALLINPUT));
        } else if (!state.playMode.IsPlaying() && !sceneFramePresented) {
            // Keep the loop paced (instead of parking in WaitMessage) while a material is open so
            // async graph cook results keep pumping; time-driven preview animation (MAT-72) is
            // carried by the per-frame preview presents in TickEditorFrame.
            if (state.sceneContext.MaterialEditor().OpenAssetId().IsValid()) {
                static_cast<void>(MsgWaitForMultipleObjects(0, nullptr, FALSE, FrameWaitMilliseconds(currentTick, nextEditorFrame), QS_ALLINPUT));
            } else {
                WaitMessage();
            }
        }
    }
}

} // namespace kb::editor

#endif
