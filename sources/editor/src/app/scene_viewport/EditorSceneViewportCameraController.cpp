#include "app/scene_viewport/EditorSceneViewportCameraController.hpp"

#if defined(_WIN32)
#include "app/EditorWindowInvalidator.hpp"
#include "app/panels/EditorPanelPointerHitContext.hpp"
#include "app/scene_viewport/EditorViewportCameraNavigationInput.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"
#include "scene/EditorSceneContext.hpp"

namespace kb::editor {
namespace {

constexpr UINT_PTR kSceneViewportCameraTimerId = 0x210BU;
constexpr UINT kSceneViewportCameraTimerMs = 16U;
constexpr float kTimerDeltaSeconds = static_cast<float>(kSceneViewportCameraTimerMs) / 1000.0F;
constexpr DWORD kToolbarFpsRefreshMs = 250U;

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] bool KeyDown(int virtualKey) noexcept {
    return (GetKeyState(virtualKey) & 0x8000) != 0;
}

[[nodiscard]] RECT ToRect(const DockRect& rect) noexcept {
    return RECT{
        .left = rect.x,
        .top = rect.y,
        .right = rect.x + rect.width,
        .bottom = rect.y + rect.height,
    };
}

[[nodiscard]] std::optional<EditorResolvedPanelContent> ResolveScenePanelContent(
    HWND messageWindow,
    HWND mainWindow,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    std::uint64_t panelId) {
    if (messageWindow == nullptr || mainWindow == nullptr || panelId == 0U) {
        return std::nullopt;
    }

    const HWND floatingWindow = floatingWindows.Queries().WindowForPanel(static_cast<std::uint32_t>(panelId));
    if (floatingWindow != nullptr && IsWindow(floatingWindow) != 0 && IsWindowVisible(floatingWindow) != 0) {
        const DockPanel* panel = dockModel.Queries().FindPanel(static_cast<std::uint32_t>(panelId));
        if (panel == nullptr || panel->kind != DockPanelKind::Scene) {
            return std::nullopt;
        }
        RECT client{};
        GetClientRect(floatingWindow, &client);
        client.top += metrics.floatingChromeHeight;
        return EditorResolvedPanelContent{
            .content = client,
            .panelId = static_cast<std::uint32_t>(panelId),
        };
    }

    if (messageWindow != mainWindow && floatingWindows.Queries().PanelId(messageWindow) != panelId) {
        return std::nullopt;
    }

    RECT client{};
    GetClientRect(mainWindow, &client);
    const DockLayout layout = dockModel.Queries().BuildLayout(
        client.right - client.left,
        client.bottom - client.top,
        metrics.menuHeight,
        metrics.toolbarHeight,
        metrics.tabStripHeight,
        metrics.tabMinWidth,
        metrics.tabWidth,
        metrics.splitterSize);
    for (const DockPanelLayout& panelLayout : layout.panels) {
        if (!panelLayout.active || panelLayout.panelId != panelId) {
            continue;
        }
        const DockPanel* panel = dockModel.Queries().FindPanel(panelLayout.panelId);
        if (panel == nullptr || panel->kind != DockPanelKind::Scene) {
            return std::nullopt;
        }
        return EditorResolvedPanelContent{
            .content = ToRect(panelLayout.content),
            .panelId = panelLayout.panelId,
        };
    }
    return std::nullopt;
}

} // namespace

EditorSceneViewportCameraController::EditorSceneViewportCameraController(
    HWND mainWindow,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& sceneViewport) noexcept
    : mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows)
    , metrics_(metrics)
    , sceneContext_(sceneContext)
    , sceneViewport_(sceneViewport) {}

UINT_PTR EditorSceneViewportCameraController::TimerId() noexcept {
    return kSceneViewportCameraTimerId;
}

bool EditorSceneViewportCameraController::HandleLeftButtonDown(HWND messageWindow, int x, int y) {
    static_cast<void>(messageWindow);
    static_cast<void>(x);
    static_cast<void>(y);
    return false;
}

bool EditorSceneViewportCameraController::HandleRightButtonDown(HWND messageWindow, int x, int y) {
    return BeginNavigation(messageWindow, x, y, KeyDown(VK_LBUTTON), true, false);
}

bool EditorSceneViewportCameraController::HandleMiddleButtonDown(HWND messageWindow, int x, int y) {
    return BeginNavigation(messageWindow, x, y, false, false, true);
}

bool EditorSceneViewportCameraController::HandlePointerMove(HWND messageWindow, int x, int y) {
    return QueueActivePointerMove(messageWindow, x, y);
}

bool EditorSceneViewportCameraController::HandleButtonUp(HWND messageWindow) {
    if (!sceneContext_.HasActiveViewportCameraNavigation()) {
        return false;
    }

    StopCapture(messageWindow);
    sceneContext_.EndViewportCameraNavigation();
    sceneViewport_.RequestPresent();
    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    return true;
}

bool EditorSceneViewportCameraController::HandleMouseWheel(HWND messageWindow, int x, int y, int wheelDelta) {
    const EditorPanelPointerHitContext panelHit =
        EditorPanelPointerHitContextResolver::Resolve(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_, x, y);
    if (!panelHit.sceneContent.has_value()) {
        return false;
    }

    const SceneViewportToolbarRects sceneRects = SceneViewportToolbarRenderer::Resolve(
        panelHit.sceneContent->content,
        sceneContext_.ViewportPreview(panelHit.sceneContent->panelId),
        sceneContext_);
    if (!PointInRect(sceneRects.renderArea, x, y)) {
        return false;
    }

    const bool adjustSpeed = sceneContext_.HasActiveViewportCameraNavigation() &&
        sceneContext_.ActiveViewportCamera() != nullptr &&
        sceneContext_.ActiveViewportCamera()->AllowsKeyboardFlight();
    EditorViewportCameraState& camera = sceneContext_.ViewportCamera(panelHit.sceneContent->panelId);
    if (!camera.ApplyWheel(static_cast<float>(wheelDelta) / static_cast<float>(WHEEL_DELTA), adjustSpeed)) {
        return false;
    }

    sceneViewport_.RequestPresent();
    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    return true;
}

bool EditorSceneViewportCameraController::HandleTimer(HWND messageWindow, WPARAM timerId) {
    if (timerId != kSceneViewportCameraTimerId) {
        return false;
    }

    if (!sceneContext_.HasActiveViewportCameraNavigation()) {
        KillTimer(messageWindow, kSceneViewportCameraTimerId);
        return true;
    }

    static_cast<void>(TickActiveNavigation(kTimerDeltaSeconds));
    return true;
}

bool EditorSceneViewportCameraController::TickActiveNavigation(float deltaSeconds) {
    if (!sceneContext_.HasActiveViewportCameraNavigation()) {
        return false;
    }

    bool changed = false;
    if (EditorViewportCameraState* camera = sceneContext_.ActiveViewportCamera(); camera != nullptr) {
        changed = camera->ApplyQueuedPointer() || changed;
    }
    changed = ApplyActiveKeyboardFlight(mainWindow_, deltaSeconds) || changed;
    if (changed) {
        sceneViewport_.RequestPresent();
        InvalidateActiveToolbar(mainWindow_);
    }
    return changed;
}

void EditorSceneViewportCameraController::Cancel(HWND messageWindow) noexcept {
    if (!sceneContext_.HasActiveViewportCameraNavigation()) {
        return;
    }
    StopCapture(messageWindow);
    sceneContext_.EndViewportCameraNavigation();
    sceneViewport_.RequestPresent();
}

bool EditorSceneViewportCameraController::BeginNavigation(HWND messageWindow, int x, int y, bool left, bool right, bool middle) {
    const EditorViewportCameraNavigationMode mode = ResolveEditorViewportCameraNavigationMode(
        left, right, middle, KeyDown(VK_MENU));
    if (mode == EditorViewportCameraNavigationMode::None) {
        return false;
    }

    const EditorPanelPointerHitContext panelHit =
        EditorPanelPointerHitContextResolver::Resolve(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_, x, y);
    if (!panelHit.sceneContent.has_value()) {
        return false;
    }

    const SceneViewportToolbarRects sceneRects = SceneViewportToolbarRenderer::Resolve(
        panelHit.sceneContent->content,
        sceneContext_.ViewportPreview(panelHit.sceneContent->panelId),
        sceneContext_);
    if (!PointInRect(sceneRects.renderArea, x, y)) {
        return false;
    }

    sceneContext_.BeginViewportCameraNavigation(panelHit.sceneContent->panelId, mode, x, y);
    StartCapture(messageWindow, right);
    sceneViewport_.RequestPresent();
    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    InvalidateActiveToolbar(messageWindow);
    return true;
}

bool EditorSceneViewportCameraController::QueueActivePointerMove(HWND messageWindow, int x, int y) {
    static_cast<void>(messageWindow);
    EditorViewportCameraState* camera = sceneContext_.ActiveViewportCamera();
    if (camera == nullptr) {
        return false;
    }

    camera->QueuePointer(x, y);
    sceneViewport_.RequestPresent();
    return true;
}

bool EditorSceneViewportCameraController::ApplyActiveKeyboardFlight(HWND messageWindow, float deltaSeconds) {
    static_cast<void>(messageWindow);
    EditorViewportCameraState* camera = sceneContext_.ActiveViewportCamera();
    if (camera == nullptr || !camera->ApplyKeyboardFlight(ReadEditorViewportCameraFlightInput(), deltaSeconds)) {
        return false;
    }

    return true;
}

void EditorSceneViewportCameraController::InvalidateActiveToolbar(HWND messageWindow) const noexcept {
    static DWORD lastRefresh = 0U;
    const DWORD now = GetTickCount();
    if (now - lastRefresh < kToolbarFpsRefreshMs) {
        return;
    }
    lastRefresh = now;

    const std::optional<EditorResolvedPanelContent> content =
        ResolveScenePanelContent(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_, sceneContext_.ActiveViewportCameraKey());
    if (!content.has_value()) {
        return;
    }

    HWND host = floatingWindows_.Queries().WindowForPanel(content->panelId);
    if (host == nullptr || IsWindow(host) == 0 || IsWindowVisible(host) == 0) {
        host = messageWindow;
    }
    const RECT toolbar = SceneViewportToolbarRenderer::Resolve(content->content, sceneContext_.ViewportPreview(content->panelId)).toolbar;
    InvalidateRect(host, &toolbar, FALSE);
}

void EditorSceneViewportCameraController::StartCapture(HWND messageWindow, bool hideCursor) noexcept {
    SetCapture(messageWindow);
    if (hideCursor) {
        HideEditorViewportNavigationCursor();
    }
}

void EditorSceneViewportCameraController::StopCapture(HWND messageWindow) noexcept {
    KillTimer(messageWindow, kSceneViewportCameraTimerId);
    RestoreEditorViewportNavigationCursor();
    if (GetCapture() == messageWindow) {
        ReleaseCapture();
    }
}

} // namespace kb::editor

#endif
