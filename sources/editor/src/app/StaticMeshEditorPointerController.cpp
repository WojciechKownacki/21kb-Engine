#include "app/StaticMeshEditorPointerController.hpp"

#include "engine/scene/SceneAssets.hpp"

#if defined(_WIN32)
#include "app/EditorKeyState.hpp"
#include "app/EditorWindowInvalidator.hpp"
#include "app/scene_viewport/EditorViewportCameraNavigationInput.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "rendering/StaticMeshEditorPanel.hpp"
#include "scene/EditorSceneContext.hpp"
#include "scene/StaticMeshEditorSession.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

} // namespace

StaticMeshEditorPointerController::StaticMeshEditorPointerController(
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

std::optional<RECT> StaticMeshEditorPointerController::Content(HWND messageWindow) const {
    if (!StaticMeshEditorSession::Instance().IsOpen()) return std::nullopt;
    return EditorPanelContentResolver::Resolve(
        DockPanelKind::StaticMeshEditor, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
}

void StaticMeshEditorPointerController::Refresh(HWND messageWindow) const noexcept {
    sceneViewport_.RequestPresent();
    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
}

bool StaticMeshEditorPointerController::BeginNavigation(HWND messageWindow, int x, int y, bool left, bool right, bool middle) {
    const std::optional<RECT> content = Content(messageWindow);
    if (!content.has_value() || !PointInRect(StaticMeshEditorPanel::ViewportRect(*content), x, y)) return false;
    StaticMeshEditorSession::Instance().Camera().BeginNavigation(
        ResolveEditorViewportCameraNavigationMode(left, right, middle, EditorKeyDown(VK_MENU)), x, y);
    SetCapture(messageWindow);
    if (right) HideEditorViewportNavigationCursor();
    sceneViewport_.RequestPresent();
    return true;
}

bool StaticMeshEditorPointerController::HandleLeftButtonDown(HWND messageWindow, int x, int y) {
    const std::optional<RECT> content = Content(messageWindow);
    if (!content.has_value() || !PointInRect(*content, x, y)) return false;
    if (EditorKeyDown(VK_MENU) && BeginNavigation(messageWindow, x, y, true, false, false)) return true;
    StaticMeshEditorSession& session = StaticMeshEditorSession::Instance();
    if (!StaticMeshEditorPanel::PointerDown(*content, x, y, session, sceneContext_.Scene().Assets().Manager())) return false;
    if (session.Ui().dragAction != 0U) SetCapture(messageWindow);
    Refresh(messageWindow);
    return true;
}

bool StaticMeshEditorPointerController::HandleRightButtonDown(HWND messageWindow, int x, int y) {
    return BeginNavigation(messageWindow, x, y, false, true, false);
}

bool StaticMeshEditorPointerController::HandleMiddleButtonDown(HWND messageWindow, int x, int y) {
    return BeginNavigation(messageWindow, x, y, false, false, true);
}

bool StaticMeshEditorPointerController::HandleMouseMove(HWND messageWindow, int x, int y) {
    StaticMeshEditorSession& session = StaticMeshEditorSession::Instance();
    if (session.Camera().IsNavigating()) {
        // One camera update per editor frame, as the other preview windows apply them.
        session.Camera().QueuePointer(x, y);
        sceneViewport_.RequestPresent();
        return true;
    }
    const std::optional<RECT> content = Content(messageWindow);
    if (!content.has_value() || !StaticMeshEditorPanel::PointerMove(*content, x, y, session)) return false;
    Refresh(messageWindow);
    return true;
}

bool StaticMeshEditorPointerController::HandleMouseWheel(HWND messageWindow, int x, int y, int wheelDelta) {
    const std::optional<RECT> content = Content(messageWindow);
    if (!content.has_value()) return false;
    StaticMeshEditorSession& session = StaticMeshEditorSession::Instance();
    const float steps = static_cast<float>(wheelDelta) / static_cast<float>(WHEEL_DELTA);
    if (PointInRect(StaticMeshEditorPanel::ViewportRect(*content), x, y)) {
        static_cast<void>(session.Camera().ApplyWheel(steps, session.Camera().AllowsKeyboardFlight()));
    } else if (!StaticMeshEditorPanel::Wheel(*content, x, y, steps, session)) {
        return false;
    }
    Refresh(messageWindow);
    return true;
}

bool StaticMeshEditorPointerController::HandleButtonUp(HWND messageWindow) {
    StaticMeshEditorSession& session = StaticMeshEditorSession::Instance();
    const bool navigating = session.Camera().IsNavigating();
    if (navigating) {
        session.Camera().EndNavigation();
        RestoreEditorViewportNavigationCursor();
    }
    const bool dragged = StaticMeshEditorPanel::PointerUp(session);
    if (!navigating && !dragged) return false;
    if (GetCapture() == messageWindow) ReleaseCapture();
    Refresh(messageWindow);
    return true;
}

} // namespace kb::editor
#endif
