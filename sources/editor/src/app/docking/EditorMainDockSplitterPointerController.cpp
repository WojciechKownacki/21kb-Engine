#include "app/docking/EditorMainDockSplitterPointerController.hpp"

#if defined(_WIN32)
#include "docking/DockMainLayoutResolver.hpp"
#include "docking/EditorDockController.hpp"
#include "docking/EditorDockModel.hpp"
#include "rendering/EditorHostSurfaceLayoutResolver.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "scene/EditorSceneContext.hpp"

namespace kb::editor {
namespace {

void RepaintNow(HWND window) noexcept {
    if (window != nullptr) {
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    }
}

} // namespace

EditorMainDockSplitterPointerController::EditorMainDockSplitterPointerController(
    HWND mainWindow,
    EditorDockModel& dockModel,
    EditorDockController& dockController,
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& sceneViewport,
    const EditorMetrics& metrics) noexcept
    : mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , dockController_(dockController)
    , sceneContext_(sceneContext)
    , sceneViewport_(sceneViewport)
    , metrics_(metrics) {}

bool EditorMainDockSplitterPointerController::HandlePointerDown(HWND messageWindow, int x, int y) {
    if (!HitsMainSplitter(messageWindow, x, y)) {
        return false;
    }
    static_cast<void>(dockController_.HandlePointerDown(messageWindow, x, y));
    static_cast<void>(dockController_.HandlePointerMove(messageWindow, x, y, true));
    EditorHostSurfaceLayoutResolver::SyncMainWindow(messageWindow, dockModel_, metrics_, sceneContext_, sceneViewport_);
    RepaintNow(messageWindow);
    return true;
}

bool EditorMainDockSplitterPointerController::HitsMainSplitter(HWND messageWindow, int x, int y) const {
    if (messageWindow != mainWindow_) {
        return false;
    }
    const DockLayout layout = DockMainLayoutResolver::Resolve(messageWindow, dockModel_, metrics_);
    return dockModel_.Queries().HitTest(layout, x, y).kind == DockHitKind::Splitter;
}

} // namespace kb::editor

#endif
