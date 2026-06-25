#include "app/pointer/EditorLeftButtonDoubleClickRouter.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserPointerHandler.hpp"
#include "app/EditorWindowInvalidator.hpp"
#include "app/inspector/EditorInspectorPointerController.hpp"
#include "docking/DockMainLayoutResolver.hpp"
#include "rendering/DockTabControlGeometry.hpp"
#include "rendering/EditorPanelContentResolver.hpp"

#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] const DockPanelLayout* TabHit(const DockLayout& layout, int x, int y) noexcept {
    for (const DockPanelLayout& panel : layout.panels) {
        if (DockTabControlGeometry::ContainsClose(panel.tab, x, y)) {
            return nullptr;
        }
        if (panel.tab.Contains(x, y)) {
            return &panel;
        }
    }
    return nullptr;
}

} // namespace

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
    if (messageWindow == mainWindow_) {
        const DockLayout layout = DockMainLayoutResolver::Resolve(mainWindow_, dockModel_, metrics_);
        if (const DockPanelLayout* tab = TabHit(layout, x, y); tab != nullptr) {
            dockModel_.Commands().ActivatePanel(tab->panelId);
            static_cast<void>(dockModel_.Commands().ToggleMaximizedLeaf(tab->leafId));
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return true;
        }
    }

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
    if (assetResult == EditorAssetBrowserDoubleClickResult::ScriptEditorOpened) {
        static_cast<void>(dockModel_.Commands().ActivatePanelKind(DockPanelKind::ScriptEditor, DockArea::Center));
    }
    if (assetResult == EditorAssetBrowserDoubleClickResult::MaterialEditorOpened) {
        static_cast<void>(dockModel_.Commands().ActivatePanelKind(DockPanelKind::MaterialEditor, DockArea::Right));
    }
    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    return true;
}

} // namespace kb::editor

#endif
