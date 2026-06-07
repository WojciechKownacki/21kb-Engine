#include "app/pointer/EditorRightButtonDownRouter.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserPointerHandler.hpp"
#include "app/EditorPendingTextEditCommitter.hpp"
#include "app/EditorWindowInvalidator.hpp"
#include "app/console/EditorConsolePointerController.hpp"
#include "app/project_files/EditorProjectFilesDeleteConfirmOverlayController.hpp"
#include "rendering/EditorPanelContentResolver.hpp"

#include <optional>

namespace kb::editor {

EditorRightButtonDownRouter::EditorRightButtonDownRouter(
    HWND mainWindow,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    EditorSceneContext& sceneContext,
    EditorPointerDragState& pointerDrag,
    const EditorMetrics& metrics) noexcept
    : mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows)
    , sceneContext_(sceneContext)
    , pointerDrag_(pointerDrag)
    , metrics_(metrics) {}

void EditorRightButtonDownRouter::Handle(HWND messageWindow, int x, int y) {
    const EditorProjectFilesDeleteConfirmOverlayController deleteConfirm(messageWindow, sceneContext_);
    if (deleteConfirm.HandlePointerDown(x, y)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    EditorPendingTextEditCommitter pendingTextEdits(sceneContext_);
    if (pendingTextEdits.CommitPendingEdits()) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }

    pointerDrag_.Clear();
    const std::optional<RECT> consoleContent = EditorPanelContentResolver::Resolve(DockPanelKind::Console, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    EditorConsolePointerController consolePointer(messageWindow, sceneContext_);
    if (consoleContent.has_value() && consolePointer.HandleContextMenu(*consoleContent, x, y)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (EditorAssetBrowserPointerHandler::HandleRightButtonDown(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }
}

} // namespace kb::editor

#endif
