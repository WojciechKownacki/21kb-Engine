#include "app/scene_viewport/EditorSceneViewportCameraController.hpp"

#if defined(_WIN32)
#include "app/EditorWindowInvalidator.hpp"
#include "app/panels/EditorPanelPointerHitContext.hpp"
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

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] bool KeyDown(int virtualKey) noexcept {
    return (GetKeyState(virtualKey) & 0x8000) != 0;
}

[[nodiscard]] bool AsyncKeyDown(int virtualKey) noexcept {
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

[[nodiscard]] EditorViewportCameraFlightInput ReadFlightInput() noexcept {
    return EditorViewportCameraFlightInput{
        .forward = AsyncKeyDown('W'),
        .backward = AsyncKeyDown('S'),
        .right = AsyncKeyDown('D'),
        .left = AsyncKeyDown('A'),
        .up = AsyncKeyDown('E'),
        .down = AsyncKeyDown('Q'),
        .boost = AsyncKeyDown(VK_SHIFT),
        .slow = AsyncKeyDown(VK_CONTROL),
    };
}

[[nodiscard]] EditorViewportCameraNavigationMode ResolveNavigationMode(bool left, bool right, bool middle) noexcept {
    const bool alt = KeyDown(VK_MENU);
    if (middle) {
        return EditorViewportCameraNavigationMode::Pan;
    }
    if (left && right) {
        return EditorViewportCameraNavigationMode::Track;
    }
    if (left) {
        return alt ? EditorViewportCameraNavigationMode::Orbit : EditorViewportCameraNavigationMode::LeftYawDolly;
    }
    if (right) {
        return alt ? EditorViewportCameraNavigationMode::Dolly : EditorViewportCameraNavigationMode::Look;
    }
    return EditorViewportCameraNavigationMode::None;
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
    return ApplyActivePointerMove(messageWindow, x, y);
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

    const SceneViewportToolbarRects sceneRects = SceneViewportToolbarRenderer::Resolve(panelHit.sceneContent->content);
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

    static_cast<void>(ApplyActiveKeyboardFlight(messageWindow, kTimerDeltaSeconds));
    return true;
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
    const EditorViewportCameraNavigationMode mode = ResolveNavigationMode(left, right, middle);
    if (mode == EditorViewportCameraNavigationMode::None) {
        return false;
    }

    const EditorPanelPointerHitContext panelHit =
        EditorPanelPointerHitContextResolver::Resolve(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_, x, y);
    if (!panelHit.sceneContent.has_value()) {
        return false;
    }

    const SceneViewportToolbarRects sceneRects = SceneViewportToolbarRenderer::Resolve(panelHit.sceneContent->content);
    if (!PointInRect(sceneRects.renderArea, x, y)) {
        return false;
    }

    sceneContext_.BeginViewportCameraNavigation(panelHit.sceneContent->panelId, mode, x, y);
    StartCapture(messageWindow);
    sceneViewport_.RequestPresent();
    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    return true;
}

bool EditorSceneViewportCameraController::ApplyActivePointerMove(HWND messageWindow, int x, int y) {
    EditorViewportCameraState* camera = sceneContext_.ActiveViewportCamera();
    if (camera == nullptr) {
        return false;
    }

    const bool changed = camera->UpdatePointer(x, y);
    static_cast<void>(ApplyActiveKeyboardFlight(messageWindow, kTimerDeltaSeconds));
    if (changed) {
        sceneViewport_.RequestPresent();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }
    return true;
}

bool EditorSceneViewportCameraController::ApplyActiveKeyboardFlight(HWND messageWindow, float deltaSeconds) {
    EditorViewportCameraState* camera = sceneContext_.ActiveViewportCamera();
    if (camera == nullptr || !camera->ApplyKeyboardFlight(ReadFlightInput(), deltaSeconds)) {
        return false;
    }

    sceneViewport_.RequestPresent();
    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    return true;
}

void EditorSceneViewportCameraController::StartCapture(HWND messageWindow) noexcept {
    SetCapture(messageWindow);
    SetTimer(messageWindow, kSceneViewportCameraTimerId, kSceneViewportCameraTimerMs, nullptr);
}

void EditorSceneViewportCameraController::StopCapture(HWND messageWindow) noexcept {
    KillTimer(messageWindow, kSceneViewportCameraTimerId);
    if (GetCapture() == messageWindow) {
        ReleaseCapture();
    }
}

} // namespace kb::editor

#endif
