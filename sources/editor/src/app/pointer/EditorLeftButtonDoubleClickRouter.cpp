#include "app/pointer/EditorLeftButtonDoubleClickRouter.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserPointerHandler.hpp"
#include "app/EditorWindowInvalidator.hpp"
#include "app/inspector/EditorInspectorPointerController.hpp"
#include "rendering/EditorPanelContentResolver.hpp"

#include <optional>

namespace kb::editor {

EditorLeftButtonDoubleClickRouter::EditorLeftButtonDoubleClickRouter(
    HWND mainWindow,
    EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    EditorSceneContext& sceneContext,
    const EditorMetrics& metrics) noexcept
    : mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows)
    , sceneContext_(sceneContext)
    , metrics_(metrics) {}

bool EditorLeftButtonDoubleClickRouter::Handle(HWND messageWindow, int x, int y) {
    const std::optional<RECT> inspectorContent = EditorPanelContentResolver::Resolve(DockPanelKind::Inspector, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    EditorInspectorPointerController inspectorPointer(sceneContext_);
    if (inspectorContent.has_value() && inspectorPointer.HandleDoubleClick(*inspectorContent, x, y)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return true;
    }

    const EditorAssetBrowserDoubleClickResult assetResult =
        EditorAssetBrowserPointerHandler::HandleDoubleClick(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_);
    if (assetResult == EditorAssetBrowserDoubleClickResult::None) {
        return false;
    }
    if (assetResult == EditorAssetBrowserDoubleClickResult::BrowserNavigation) {
        sceneContext_.ClearHierarchySelection();
    }
    if (assetResult == EditorAssetBrowserDoubleClickResult::ScriptEditorOpened) {
        for (const DockPanel& panel : dockModel_.Queries().Panels()) {
            if (panel.kind == DockPanelKind::ScriptEditor) {
                dockModel_.Commands().ActivatePanel(panel.id);
                break;
            }
        }
    }
    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    return true;
}

} // namespace kb::editor

#endif
