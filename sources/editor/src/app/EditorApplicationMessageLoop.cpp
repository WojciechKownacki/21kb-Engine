#include "app/EditorApplicationMessageLoop.hpp"

#if defined(_WIN32)

#include "docking/EditorFloatingWindowManager.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "rendering/ScenePanelContentRenderer.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"
#include "app/scene_viewport/EditorSceneViewportCameraController.hpp"
#include "app/scene_viewport/EditorSceneViewportObjectInteraction.hpp"

#include <algorithm>
#include <chrono>
#include <optional>

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

[[nodiscard]] bool PresentScenePanel(EditorApplicationState& state, HWND host, const DockPanel& panel, const RECT& content, bool refreshToolbar) {
    if (host == nullptr || IsWindow(host) == 0 || IsWindowVisible(host) == 0) {
        return false;
    }

    ScenePanelContentRenderer::PresentViewport(state.sceneViewport, host, content, panel, state.sceneContext);
    if (refreshToolbar) {
        InvalidateSceneToolbar(host, content, state.sceneContext.ViewportPreview(panel.id));
    }
    return true;
}

[[nodiscard]] bool PresentMainScenePanels(EditorApplicationState& state, bool refreshToolbar) {
    if (state.window == nullptr || IsWindowVisible(state.window) == 0) {
        return false;
    }

    bool presented = false;
    const DockLayout layout = BuildMainLayout(state);
    for (const DockPanelLayout& panelLayout : layout.panels) {
        if (!panelLayout.active) {
            continue;
        }
        const DockPanel* panel = state.dockModel.Queries().FindPanel(panelLayout.panelId);
        if (panel == nullptr || panel->kind != DockPanelKind::Scene) {
            continue;
        }
        presented = PresentScenePanel(state, state.window, *panel, ToRect(panelLayout.content), refreshToolbar) || presented;
    }
    return presented;
}

[[nodiscard]] bool PresentFloatingScenePanels(EditorApplicationState& state, bool refreshToolbar) {
    bool presented = false;
    const EditorFloatingWindowQueries queries = state.floatingWindows.Queries();
    for (HWND window : queries.Windows()) {
        if (window == nullptr || IsWindow(window) == 0 || IsWindowVisible(window) == 0) {
            continue;
        }
        const DockPanel* panel = state.dockModel.Queries().FindPanel(queries.PanelId(window));
        if (panel == nullptr || panel->kind != DockPanelKind::Scene) {
            continue;
        }
        RECT content{};
        GetClientRect(window, &content);
        content.top += state.metrics.floatingChromeHeight;
        presented = PresentScenePanel(state, window, *panel, content, refreshToolbar) || presented;
    }
    return presented;
}

[[nodiscard]] bool PresentVisibleScenePanels(EditorApplicationState& state) {
    const bool refreshToolbar = ShouldRefreshSceneToolbars();
    const bool mainPresented = PresentMainScenePanels(state, refreshToolbar);
    const bool floatingPresented = PresentFloatingScenePanels(state, refreshToolbar);
    return mainPresented || floatingPresented;
}

void TickPlayMode(EditorApplicationState& state, float deltaSeconds) {
    if (!state.playMode.IsPlaying()) {
        return;
    }
    static_cast<void>(state.sceneContext.Scene().Runtime().Update(deltaSeconds));
    state.sceneContext.MarkSceneRenderDirty();
    if (state.sceneContext.Scene().Runtime().ShouldQuit()) {
        state.playMode.Stop();
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

    return PresentVisibleScenePanels(state) || navigationChanged;
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
    while (state.running) {
        int pumpedMessages = 0;
        while (pumpedMessages < kMaxMessagesPerPump && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
            ++pumpedMessages;
            if (message.message == WM_QUIT) {
                state.running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
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
        const bool sceneFramePresented = TickEditorFrame(state, deltaSeconds);
        nextEditorFrame = currentTick + editorFrameInterval;

        if (state.playMode.IsPaused()) {
            if (state.window != nullptr) {
                InvalidateRect(state.window, nullptr, FALSE);
            }
            static_cast<void>(MsgWaitForMultipleObjects(0, nullptr, FALSE, kPausedToolbarAnimationIntervalMs, QS_ALLINPUT));
        } else if (!state.playMode.IsPlaying() && !sceneFramePresented) {
            WaitMessage();
        }
    }
}

} // namespace kb::editor

#endif
