#include "app/pointer/EditorRightButtonDownRouter.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserPointerHandler.hpp"
#include "app/EditorPendingTextEditCommitter.hpp"
#include "app/EditorWindowInvalidator.hpp"
#include "app/console/EditorConsolePointerController.hpp"
#include "app/project_files/EditorProjectFilesDeleteConfirmOverlayController.hpp"
#include "app/scene_viewport/EditorSceneViewportCameraController.hpp"
#include "engine/scene/LightComponent.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "scene/EditorHierarchyRowPicker.hpp"

#include <optional>

namespace kb::editor {
namespace {

constexpr UINT_PTR kHierarchyMenuCreateEmpty = 1001;
constexpr UINT_PTR kHierarchyMenuDirectionalLight = 1101;
constexpr UINT_PTR kHierarchyMenuPointLight = 1102;
constexpr UINT_PTR kHierarchyMenuSpotLight = 1103;

void AppendDisabled(HMENU menu, const char* label) {
    AppendMenuA(menu, MF_STRING | MF_GRAYED, 0, label);
}

void AppendSeparator(HMENU menu) {
    AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
}

[[nodiscard]] HMENU CreateHierarchySystemMenu() {
    HMENU menu = CreatePopupMenu();
    HMENU lightMenu = CreatePopupMenu();

    AppendMenuA(menu, MF_STRING, kHierarchyMenuCreateEmpty, "Create Entity\tCtrl+Shift+N");
    AppendMenuA(lightMenu, MF_STRING, kHierarchyMenuDirectionalLight, "Directional Light");
    AppendMenuA(lightMenu, MF_STRING, kHierarchyMenuPointLight, "Point Light");
    AppendMenuA(lightMenu, MF_STRING, kHierarchyMenuSpotLight, "Spot Light");
    AppendMenuA(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(lightMenu), "Lighting");
    AppendSeparator(menu);
    AppendDisabled(menu, "Delete\tDel");
    AppendDisabled(menu, "Duplicate\tCtrl+D");
    AppendDisabled(menu, "Rename");
    AppendDisabled(menu, "Paste Special");
    AppendDisabled(menu, "Paste\tCtrl+V");
    AppendDisabled(menu, "Copy\tCtrl+C");
    AppendDisabled(menu, "Cut\tCtrl+X");
    return menu;
}

[[nodiscard]] UINT ShowHierarchySystemMenu(HWND window, int x, int y) {
    HMENU menu = CreateHierarchySystemMenu();
    if (menu == nullptr) {
        return 0;
    }

    POINT screenPoint{ x, y };
    ClientToScreen(window, &screenPoint);
    SetForegroundWindow(window);
    const UINT command = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        screenPoint.x,
        screenPoint.y,
        0,
        window,
        nullptr);
    DestroyMenu(menu);
    return command;
}

[[nodiscard]] bool ExecuteHierarchySystemMenuCommand(UINT command, EditorSceneContext& sceneContext) {
    switch (command) {
    case kHierarchyMenuCreateEmpty:
        return sceneContext.CreateHierarchyObject().IsValid();
    case kHierarchyMenuDirectionalLight:
        return sceneContext.CreateLightObject(kb::scene::LightKind::Directional).IsValid();
    case kHierarchyMenuPointLight:
        return sceneContext.CreateLightObject(kb::scene::LightKind::Point).IsValid();
    case kHierarchyMenuSpotLight:
        return sceneContext.CreateLightObject(kb::scene::LightKind::Spot).IsValid();
    default:
        return false;
    }
}

} // namespace

EditorRightButtonDownRouter::EditorRightButtonDownRouter(
    HWND mainWindow,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& sceneViewport,
    EditorPointerDragState& pointerDrag,
    const EditorMetrics& metrics) noexcept
    : mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows)
    , sceneContext_(sceneContext)
    , sceneViewport_(sceneViewport)
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
    EditorSceneViewportCameraController sceneCamera(mainWindow_, dockModel_, floatingWindows_, metrics_, sceneContext_, sceneViewport_);
    if (sceneCamera.HandleRightButtonDown(messageWindow, x, y)) {
        return;
    }

    const std::optional<RECT> consoleContent = EditorPanelContentResolver::Resolve(DockPanelKind::Console, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    EditorConsolePointerController consolePointer(messageWindow, sceneContext_);
    if (consoleContent.has_value() && consolePointer.HandleContextMenu(*consoleContent, x, y)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    const std::optional<RECT> hierarchyContent = EditorPanelContentResolver::Resolve(DockPanelKind::Hierarchy, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    if (hierarchyContent.has_value() && x >= hierarchyContent->left && x < hierarchyContent->right && y >= hierarchyContent->top && y < hierarchyContent->bottom) {
        const kb::scene::SceneEntity entity = EditorHierarchyRowPicker::EntityAtContentPoint(*hierarchyContent, x, y, sceneContext_);
        if (entity.IsValid() && !sceneContext_.IsHierarchyEntitySelected(entity)) {
            sceneContext_.SelectEntity(entity);
        }
        sceneContext_.CloseHierarchyContextMenu();
        const UINT command = ShowHierarchySystemMenu(messageWindow, x, y);
        if (ExecuteHierarchySystemMenuCommand(command, sceneContext_)) {
            sceneViewport_.RequestPresent();
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (sceneContext_.IsHierarchyContextMenuOpen()) {
        sceneContext_.CloseHierarchyContextMenu();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }

    if (EditorAssetBrowserPointerHandler::HandleRightButtonDown(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }
}

} // namespace kb::editor

#endif
