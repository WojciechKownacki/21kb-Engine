#include "app/pointer/EditorRightButtonDownRouter.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserPointerHandler.hpp"
#include "app/EditorPendingTextEditCommitter.hpp"
#include "app/EditorWindowInvalidator.hpp"
#include "app/console/EditorConsolePointerController.hpp"
#include "app/project_files/EditorProjectFilesDeleteConfirmOverlayController.hpp"
#include "app/project_files/EditorProjectFilesTransientUiController.hpp"
#include "app/scene_viewport/EditorSceneViewportCameraController.hpp"
#include "engine/scene/LightComponent.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "rendering/SkeletalMeshEditorPanelRenderer.hpp"
#include "scene/EditorHierarchyRowPicker.hpp"

#include <algorithm>
#include <optional>

namespace kb::editor {
namespace {

constexpr UINT_PTR kHierarchyMenuCreateEmpty = 1001;
constexpr UINT_PTR kHierarchyMenuDelete = 1002;
constexpr UINT_PTR kHierarchyMenuDuplicate = 1003;
constexpr UINT_PTR kHierarchyMenuRename = 1004;
constexpr UINT_PTR kHierarchyMenuDirectionalLight = 1101;
constexpr UINT_PTR kHierarchyMenuPointLight = 1102;
constexpr UINT_PTR kHierarchyMenuSpotLight = 1103;
constexpr UINT_PTR kSkeletonTreeMenuAddSocket = 1201;
constexpr UINT_PTR kSkeletonTreeMenuRenameSocket = 1202;
constexpr UINT_PTR kSkeletonTreeMenuDeleteSocket = 1203;

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
    AppendMenuA(menu, MF_STRING, kHierarchyMenuDelete, "Delete\tDel");
    AppendMenuA(menu, MF_STRING, kHierarchyMenuDuplicate, "Duplicate\tCtrl+D");
    AppendMenuA(menu, MF_STRING, kHierarchyMenuRename, "Rename\tF2");
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
    case kHierarchyMenuDelete:
        return sceneContext.DeleteSelectedHierarchyEntity();
    case kHierarchyMenuDuplicate:
        return sceneContext.DuplicateSelectedHierarchyEntities();
    case kHierarchyMenuRename:
        return sceneContext.BeginHierarchyRename();
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

[[nodiscard]] HMENU CreateSkeletonTreeSystemMenu(bool canAddSocket) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return nullptr;
    AppendMenuA(
        menu,
        MF_STRING | (canAddSocket ? MF_ENABLED : MF_GRAYED),
        kSkeletonTreeMenuAddSocket,
        "Add Socket");
    AppendMenuA(menu, MF_STRING | MF_GRAYED, kSkeletonTreeMenuRenameSocket, "Rename Socket");
    AppendMenuA(menu, MF_STRING | MF_GRAYED, kSkeletonTreeMenuDeleteSocket, "Delete Socket");
    return menu;
}

[[nodiscard]] UINT ShowSkeletonTreeSystemMenu(HWND window, int x, int y, bool canAddSocket) {
    HMENU menu = CreateSkeletonTreeSystemMenu(canAddSocket);
    if (menu == nullptr) return 0;

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

    const std::optional<RECT> materialEditorContent = EditorPanelContentResolver::Resolve(DockPanelKind::MaterialEditor, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    if (materialEditorContent.has_value() && x >= materialEditorContent->left && x < materialEditorContent->right && y >= materialEditorContent->top && y < materialEditorContent->bottom) {
        // A right-click while the node palette / wire-drop menu is open dismisses it (UE-style) instead of
        // beginning a canvas pan. Panning under the open, screen-anchored palette slid the whole graph
        // around beneath it, which reads as "the dropdown can be dragged". Drop any parked wire too, so a
        // dismissed wire-drop menu leaves the pin unplugged rather than half-connected.
        if (sceneContext_.IsMaterialGraphContextMenuOpen()) {
            static_cast<void>(sceneContext_.CloseMaterialGraphContextMenu());
            static_cast<void>(sceneContext_.CancelMaterialGraphPinConnection());
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
        const MaterialEditorPanelLayout layout = MaterialEditorPanelRenderer::ResolveLayout(*materialEditorContent);
        if (sceneContext_.MaterialEditor().InfoPanelVisible() &&
            MaterialEditorPanelRectWidth(layout.detailsPanel) >= 220 &&
            MaterialEditorPanelRectHeight(layout.detailsPanel) >= 140 &&
            MaterialEditorPanelPointInRect(layout.detailsPanel, x, y)) {
            static_cast<void>(sceneContext_.CloseMaterialGraphContextMenu());
            static_cast<void>(sceneContext_.CancelMaterialGraphPinConnection());
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
        if (MaterialEditorPanelPointInRect(layout.graphCanvas, x, y)) {
            sceneContext_.AssetBrowser().ClearSelection();
            sceneContext_.ClearHierarchySelection();
            sceneContext_.FocusMaterialGraph(true);
            EditorProjectFilesTransientUiController(sceneContext_).CloseTransientUi();
            const kb::assets::AssetId materialId = sceneContext_.MaterialEditor().OpenAssetId();
            const std::optional<kb::render::RenderMaterialAssetData> material = sceneContext_.MaterialEditor().WorkingCopy().has_value()
                ? sceneContext_.MaterialEditor().WorkingCopy()
                : sceneContext_.ReadMaterialDocumentAsset(materialId);
            if (material.has_value()) {
                if (const std::optional<std::uint32_t> nodeId = MaterialEditorPanelRenderer::GraphNodeAt(*materialEditorContent, material->graph, sceneContext_, materialId, x, y)) {
                    static_cast<void>(sceneContext_.SelectMaterialGraphContextTarget(*nodeId, 0U));
                } else if (const std::optional<std::uint32_t> commentId = MaterialEditorPanelRenderer::GraphCommentAt(
                               *materialEditorContent,
                               material->graph,
                               sceneContext_,
                               materialId,
                               x,
                               y)) {
                    static_cast<void>(sceneContext_.SelectMaterialGraphContextTarget(0U, *commentId));
                }
            }
            static_cast<void>(sceneContext_.CloseMaterialGraphContextMenu());
            static_cast<void>(sceneContext_.BeginMaterialGraphPan(x, y));
            SetCapture(messageWindow);
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
    }

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

    const std::optional<RECT> skeletalMeshEditorContent = EditorPanelContentResolver::Resolve(
        DockPanelKind::SkeletalMeshEditor, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    if (skeletalMeshEditorContent.has_value() && sceneContext_.HasSkeletalMeshEditorAsset()) {
        const std::optional<SkeletalMeshEditorTreeRow> row =
            SkeletalMeshEditorPanelRenderer::TreeRowAt(*skeletalMeshEditorContent, sceneContext_, x, y);
        if (row.has_value()) {
            bool changed = row->kind == SkeletalMeshEditorTreeItemKind::Bone
                ? sceneContext_.SelectSkeletalMeshEditorBone(row->boneId)
                : sceneContext_.SelectSkeletalMeshEditorSocket(row->socketName);
            const UINT command = ShowSkeletonTreeSystemMenu(
                messageWindow, x, y, sceneContext_.CanAddSkeletonEditorSocket());
            if (command == kSkeletonTreeMenuAddSocket) {
                changed = sceneContext_.AddSkeletonEditorSocket() || changed;
            }
            if (changed) {
                sceneViewport_.RequestPresent();
            }
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
    }

    const std::optional<RECT> hierarchyContent = EditorPanelContentResolver::Resolve(DockPanelKind::Hierarchy, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    if (hierarchyContent.has_value() && x >= hierarchyContent->left && x < hierarchyContent->right && y >= hierarchyContent->top && y < hierarchyContent->bottom) {
        const kb::scene::SceneEntity entity = EditorHierarchyRowPicker::EntityAtContentPoint(*hierarchyContent, x, y, sceneContext_);
        if (entity.IsValid() && !sceneContext_.IsHierarchyEntitySelected(entity)) {
            sceneContext_.SelectEntity(entity);
        }
        const UINT command = ShowHierarchySystemMenu(messageWindow, x, y);
        if (ExecuteHierarchySystemMenuCommand(command, sceneContext_)) {
            sceneViewport_.RequestPresent();
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (EditorAssetBrowserPointerHandler::HandleRightButtonDown(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }
}

} // namespace kb::editor

#endif
