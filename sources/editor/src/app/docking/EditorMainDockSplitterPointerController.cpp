#include "app/docking/EditorMainDockSplitterPointerController.hpp"

#if defined(_WIN32)
#include "docking/DockMainLayoutResolver.hpp"
#include "docking/EditorDockController.hpp"
#include "docking/EditorDockModel.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"

namespace kb::editor {

EditorMainDockSplitterPointerController::EditorMainDockSplitterPointerController(
    HWND mainWindow,
    EditorDockModel& dockModel,
    EditorDockController& dockController,
    EditorSceneBgfxViewport& sceneViewport,
    const EditorMetrics& metrics) noexcept
    : mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , dockController_(dockController)
    , sceneViewport_(sceneViewport)
    , metrics_(metrics) {}

bool EditorMainDockSplitterPointerController::HandlePointerDown(HWND messageWindow, int x, int y) {
    if (!HitsMainSplitter(messageWindow, x, y)) {
        return false;
    }
    static_cast<void>(dockController_.HandlePointerDown(messageWindow, x, y));
    sceneViewport_.RequestPresent();
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
